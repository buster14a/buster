// The implicit postorder syntax tree (c_ast.h, GitHub #3102): precedence and
// associativity, declarators, typedef shadowing, labels, tags and
// initializers, function definitions, and the structural invariants every
// build must keep. Each case builds the same source with every refill batch
// and every layout and requires one tree: refill-boundary invariance proves no
// rule reads past C_AST_LOOKAHEAD, and layout invariance proves the accessors
// answer the same in IMPLICIT, HYBRID and EXPLICIT; small column capacities
// paired with the batches move the columns mid-build, which must not change
// the tree either. Deep nesting must succeed without growing the C stack, and
// truncated input must fail with one diagnostic and no partial tree.
// c_ast_test_columns covers the column arenas: step boundaries, moves, a
// phase-arena peak that does not grow with the tree, and c_ast_release.
// Every test releases the trees it builds, then the units they read through
// c_test_scratch_end; C_TEST_BOUNDED_FIXTURE checks that each fixture returned
// every reservation it made (GitHub #3306). c_ast_test_corpus (see "corpus
// differential" below) builds every tests/**/*.c file and the compiler's own
// frontend sources and holds the tree to c_parse_ast's top-level declaration
// split, the declaration split read from the tree (c_parse_ast_from_tree) to
// c_parse_ast's records field by field (c_ast_corpus_split), and the tree
// expression typer (c_ast_types.c) to the type machine (c_ast_corpus_types),
// where the analysis with the tree must also equal the analysis without it in
// every model row (c_ast_corpus_models_differ).
// c_ast_test_sizeof_handoff runs the same inputs through the sizeof handoff
// comparison (c_test_sizeof_handoff_set), which holds lowering's `sizeof`
// sizes to semantic analysis's answers.
// c_ast_test_types probes the typer's accepted kinds, declines and misses one
// range at a time; c_ast_test_type_lookups holds its name checks to the
// machine on shadowing shapes; c_ast_test_split runs the tree split on one
// shape per fallback reason in every layout; c_ast_test_declaration_hints holds
// the statement plan's declaration hints (storage classes, the auto precheck)
// to the walker. c_ast_test_spans checks the retained token spans and their
// range lookup, which the corpus also holds to every node
// (c_ast_test_spans_consistent).
//
// Case helpers (macros: they add into the caller's `result`):
//   c_ast_test_expect(arguments, source, expected_dump)
//   c_ast_test_expect_failure(arguments, source)
//   c_ast_test_expect_dialect(arguments, source, dialect, expected_dump)
//   c_ast_test_expect_failure_dialect(arguments, source, dialect)
//   c_ast_test_expect_expression(arguments, expression, expected_dump)
//   c_ast_test_expect_kind(arguments, source, kind, expected_dump)
#include <buster/tests/compiler/frontend/c/c_ast_test.h>
#if BUSTER_INCLUDE_TESTS
#include <buster/lib/compiler/driver/driver.h>
#include <buster/lib/compiler/frontend/c/c_ast.h>
#include <buster/lib/compiler/frontend/c/c_gen_internal.h>
#include <buster/lib/compiler/frontend/c/c_parse_internal.h>
#include <buster/lib/compiler/frontend/c/c_source_internal.h>
#include <buster/lib/string.h>
#include <buster/lib/file.h>
#include <buster/lib/os.h>
#include <buster/lib/system_headers.h>

BUSTER_GLOBAL_LOCAL u32 const c_ast_test_batches[] = {0, 1, 2, 3, 7, 64};
// The first column capacity of the build at the same index: small ones move the
// columns while the tree grows, which must not change it.
BUSTER_GLOBAL_LOCAL u32 const c_ast_test_capacities[] = {0, 1, 3, 0, 7, 64};
BUSTER_CT_CHECK(BUSTER_ARRAY_LENGTH(c_ast_test_capacities) == BUSTER_ARRAY_LENGTH(c_ast_test_batches));
BUSTER_GLOBAL_LOCAL CAstLayout const c_ast_test_layouts[] = {C_AST_LAYOUT_IMPLICIT, C_AST_LAYOUT_HYBRID, C_AST_LAYOUT_EXPLICIT};

BUSTER_GLOBAL_LOCAL CPreprocessResult c_ast_test_preprocess(Arena* arena, String8 source, CPreprocessDialect dialect)
{
    return c_preprocess(arena, source,
                        (CPreprocessOptions){
                            .source_path = S8("ast-test.c"),
                            .target = target_native,
                            .data_layout = target_data_layout(target_native),
                            .dialect = dialect,
                        });
}

BUSTER_GLOBAL_LOCAL UnitTestResult c_ast_test_source_anchors(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    TemporalArena temporary = scratch_begin(&arguments->arena, 1);
    String8 sources[] = {
        S8("int name = 1; char const* text = \"abc\";"),
        S8("#define JOIN(a,b) a##b\n#line 73 \"mapped.c\"\nint JOIN(mapped,_name) = 2;"),
    };
    char8* oversized = arena_allocate(temporary.arena, char8, 70004);
    oversized[0] = '"';
    memset(oversized + 1, 'a', 70000);
    oversized[70001] = '"';
    oversized[70002] = ';';
    oversized[70003] = 0;
    for (u32 form = 0; form < BUSTER_ARRAY_LENGTH(sources) + 1; form += 1)
    {
        String8 source = form < BUSTER_ARRAY_LENGTH(sources) ? sources[form] : (String8){.pointer = oversized, .length = 70003};
        CPreprocessResult preprocess = c_ast_test_preprocess(temporary.arena, source, C_PREPROCESS_DIALECT_GNU17);
        BUSTER_TEST(arguments, !preprocess.error_count);
        CParserResult parser = c_parse_ast_reference(temporary.arena, preprocess);
        CPreprocessResult foreign = preprocess;
        foreign.tokens += 1;
        IrSourceMapCursor cursor = IR_SOURCE_MAP_CURSOR_EMPTY;
        bool large = false;
        bool number = false;
        for (u32 index = 0; index < preprocess.token_count; index += 1)
        {
            CToken token = preprocess.tokens[index];
            CNumberFact fact = c_source_number_fact(&preprocess, parser.number_facts, index);
            number |= fact.present;
            BUSTER_TEST(arguments, !c_source_number_fact(&foreign, parser.number_facts, index).present &&
                                   !c_source_number_fact(&preprocess, 0, index).present);
            CSourceAnchor anchor = c_source_anchor_at(&preprocess, index);
            String8 spelling = c_token_spelling(preprocess.spelling_base, token);
            BUSTER_TEST(arguments, anchor.offset == token.offset && anchor.length == spelling.length);
            BUSTER_TEST(arguments, string_equal(c_source_anchor_spelling(&preprocess, anchor), spelling));
            CSourceSite old_site = c_preprocess_token_site(&preprocess, token);
            CSourceSite site = c_source_anchor_site_cursor(&preprocess, anchor, &cursor);
            BUSTER_TEST(arguments, site.map_offset_plus_one == old_site.map_offset_plus_one && site.file == old_site.file &&
                                   c_source_anchor_source_cursor(&preprocess, anchor, &cursor) == site.file);
            CSourceLocation old_location = c_preprocess_token_location(&preprocess, token);
            CSourceLocation location = c_source_anchor_location_cursor(&preprocess, anchor, &cursor);
            BUSTER_TEST(arguments, !memcmp(&old_location, &location, sizeof(location)));
            if (spelling.length)
            {
                CSourceAnchor named = c_source_anchor_from_name(&preprocess, spelling);
                BUSTER_TEST(arguments, named.offset == anchor.offset && named.length == anchor.length);
            }
            // Retained provenance remains usable after token grammar has
            // become unavailable, including sentinel-sized spellings.
            preprocess.tokens[index].kind = C_TOKEN_INVALID;
            preprocess.tokens[index].symbol = UINT32_MAX;
            preprocess.tokens[index].punctuator = C_PUNCTUATOR_ELLIPSIS;
            CSourceAnchor poisoned = c_source_anchor_at(&preprocess, index);
            BUSTER_TEST(arguments, poisoned.offset == anchor.offset && poisoned.length == anchor.length &&
                                   string_equal(c_source_anchor_spelling(&preprocess, poisoned), spelling));
            CSourceLocation poisoned_location = c_source_anchor_location_cursor(&preprocess, poisoned, &cursor);
            BUSTER_TEST(arguments, !memcmp(&old_location, &poisoned_location, sizeof(poisoned_location)));
            CNumberFact poisoned_fact = c_source_number_fact(&preprocess, parser.number_facts, index);
            BUSTER_TEST(arguments, fact.present == poisoned_fact.present && fact.flags == poisoned_fact.flags && fact.value == poisoned_fact.value);
            large |= anchor.length == 70002;
        }
        BUSTER_TEST(arguments, form < BUSTER_ARRAY_LENGTH(sources) || large);
        BUSTER_TEST(arguments, form >= BUSTER_ARRAY_LENGTH(sources) || number);
    }
    c_test_scratch_end(temporary);
    return result;
}

// The two trees are one tree: the same kinds, extents, anchors and payloads,
// where a LIST node's payload is compared through its list count (HYBRID
// replaces it with a slice offset), and the same children from every
// accessor.
BUSTER_GLOBAL_LOCAL bool c_ast_test_same_tree(CAst const* left, CAst const* right, bool compare_children)
{
    bool same = left->node_count == right->node_count && left->root == right->root && left->parenthesis_count == right->parenthesis_count;
    for (u32 index = 0; index < left->parenthesis_count && same; index += 1)
    {
        CAstParenthesis a = left->parentheses[index];
        CAstParenthesis b = right->parentheses[index];
        same = a.node == b.node && a.open == b.open && a.close == b.close && a.flags == b.flags;
    }
    for (u32 node = 0; node < left->node_count && same; node += 1)
    {
        same = left->kinds[node] == right->kinds[node] && left->extents[node] == right->extents[node] && left->tokens[node] == right->tokens[node];
        if (same)
        {
            if (c_ast_kind_contract((CAstKind)left->kinds[node]) == C_AST_CONTRACT_LIST)
            {
                same = c_ast_list_count(left, node) == c_ast_list_count(right, node);
            }
            else
            {
                same = left->data[node] == right->data[node];
            }
        }
        if (same && compare_children)
        {
            u32 count = c_ast_child_count(left, node);
            same = count == c_ast_child_count(right, node);
            u32 roots_left[8];
            u32 roots_right[8];
            same = same && c_ast_children(left, node, roots_left, 8) == count && c_ast_children(right, node, roots_right, 8) == count;
            for (u32 index = 0; index < count && index < 8 && same; index += 1)
            {
                same = roots_left[index] == roots_right[index] && c_ast_child_at(left, node, index) == roots_left[index] &&
                       c_ast_child_at(right, node, index) == roots_left[index];
            }
            same = same && c_ast_child_at(left, node, count) == C_AST_NODE_INVALID;
        }
    }
    return same;
}

BUSTER_GLOBAL_LOCAL u32 c_ast_test_find_kind(CAst const* ast, CAstKind kind)
{
    u32 result = C_AST_NODE_INVALID;
    for (u32 node = 0; node < ast->node_count && result == C_AST_NODE_INVALID; node += 1)
    {
        if (ast->kinds[node] == kind)
        {
            result = node;
        }
    }
    return result;
}

// Builds `source` under every batch and layout. With expect_failure the
// build must fail identically each time; otherwise the dump of the first
// `focus` node (the root when focus_kind is the translation unit) must equal
// `expected`. When `focus_child` is set the focus node's first child is
// dumped instead (an expression statement's expression).
BUSTER_GLOBAL_LOCAL UnitTestResult c_ast_test_check(UnitTestArguments* arguments, String8 source, CPreprocessDialect dialect, CAstKind focus_kind,
                                                    bool focus_child, String8 expected, bool expect_failure)
{
    UnitTestResult result = {0};
    TemporalArena temporary = scratch_begin(&arguments->arena, 1);
    CPreprocessResult preprocess = c_ast_test_preprocess(temporary.arena, source, dialect);
    if (BUSTER_REQUIRE(arguments, preprocess.error_count == 0))
    {
        CAstResult reference = c_ast_build(temporary.arena, preprocess, (CAstOptions){0});
        if (expect_failure)
        {
            BUSTER_TEST(arguments, !reference.complete);
            BUSTER_TEST(arguments, reference.ast.node_count == 0);
            if (BUSTER_REQUIRE(arguments, reference.diagnostic_count == 1))
            {
                BUSTER_TEST(arguments, reference.diagnostics[0].severity == C_DIAGNOSTIC_ERROR);
                BUSTER_TEST(arguments, reference.diagnostics[0].message.length > 0);
            }
        }
        else if (BUSTER_REQUIRE(arguments, reference.complete && reference.diagnostic_count == 0))
        {
            BUSTER_TEST(arguments, c_ast_validate(&reference.ast) == C_AST_NODE_INVALID);
            BUSTER_TEST(arguments, reference.ast.root == reference.ast.node_count - 1);
            BUSTER_TEST(arguments, reference.ast.kinds[reference.ast.root] == C_AST_TRANSLATION_UNIT);
            u32 focus = focus_kind == C_AST_TRANSLATION_UNIT ? reference.ast.root : c_ast_test_find_kind(&reference.ast, focus_kind);
            if (BUSTER_REQUIRE(arguments, focus != C_AST_NODE_INVALID))
            {
                if (focus_child)
                {
                    focus = c_ast_child_at(&reference.ast, focus, 0);
                }
                String8 actual = c_ast_dump(temporary.arena, &reference.ast, preprocess, focus);
                bool equal = string_equal(actual, expected);
                if (!equal)
                {
                    string_print_error(S8("c_ast dump mismatch for: {S8}\n  expected: {S8}\n  actual:   {S8}\n"), source, expected, actual);
                }
                BUSTER_TEST(arguments, equal);
            }
        }
        for (u32 batch = 0; batch < BUSTER_ARRAY_LENGTH(c_ast_test_batches); batch += 1)
        {
            for (u32 layout = 0; layout < BUSTER_ARRAY_LENGTH(c_ast_test_layouts); layout += 1)
            {
                CAstResult other = c_ast_build(temporary.arena, preprocess,
                                               (CAstOptions){.layout = c_ast_test_layouts[layout], .refill_batch = c_ast_test_batches[batch],
                                                             .column_capacity = c_ast_test_capacities[batch]});
                BUSTER_TEST(arguments, other.complete == reference.complete);
                BUSTER_TEST(arguments, other.diagnostic_count == reference.diagnostic_count);
                if (expect_failure)
                {
                    if (other.diagnostic_count == 1 && reference.diagnostic_count == 1)
                    {
                        BUSTER_STRING_TEST(arguments, other.diagnostics[0].message, reference.diagnostics[0].message);
                        BUSTER_TEST(arguments, other.diagnostics[0].location.offset == reference.diagnostics[0].location.offset);
                    }
                }
                else if (other.complete && reference.complete)
                {
                    BUSTER_TEST(arguments, other.ast.layout == c_ast_test_layouts[layout]);
                    BUSTER_TEST(arguments, c_ast_validate(&other.ast) == C_AST_NODE_INVALID);
                    BUSTER_TEST(arguments, c_ast_test_same_tree(&reference.ast, &other.ast, true));
                    String8 left = c_ast_dump(temporary.arena, &reference.ast, preprocess, reference.ast.root);
                    String8 right = c_ast_dump(temporary.arena, &other.ast, preprocess, other.ast.root);
                    BUSTER_STRING_TEST(arguments, left, right);
                }
                c_ast_release(&other.ast);
            }
        }
        c_ast_release(&reference.ast);
    }
    c_test_scratch_end(temporary);
    return result;
}

BUSTER_GLOBAL_LOCAL void c_ast_test_merge(UnitTestResult* into, UnitTestResult part)
{
    into->test_count += part.test_count;
    into->succeeded_test_count += part.succeeded_test_count;
}

#define c_ast_test_expect_dialect(arguments, source, dialect, expected) \
    c_ast_test_merge(&result, c_ast_test_check((arguments), (source), (dialect), C_AST_TRANSLATION_UNIT, false, (expected), false))
#define c_ast_test_expect(arguments, source, expected) c_ast_test_expect_dialect((arguments), (source), C_PREPROCESS_DIALECT_GNU17, (expected))
#define c_ast_test_expect_failure_dialect(arguments, source, dialect) \
    c_ast_test_merge(&result, c_ast_test_check((arguments), (source), (dialect), C_AST_TRANSLATION_UNIT, false, S8(""), true))
#define c_ast_test_expect_failure(arguments, source) c_ast_test_expect_failure_dialect((arguments), (source), C_PREPROCESS_DIALECT_GNU17)
#define c_ast_test_expect_kind(arguments, source, kind, expected) \
    c_ast_test_merge(&result, c_ast_test_check((arguments), (source), C_PREPROCESS_DIALECT_GNU17, (kind), false, (expected), false))

// An expression statement inside a function with a few objects and a typedef
// in scope; the dump is the expression alone.
BUSTER_GLOBAL_LOCAL UnitTestResult c_ast_test_check_expression(UnitTestArguments* arguments, String8 expression, String8 expected)
{
    String8 parts[] = {
        S8("typedef int T; int a, b, c, d, e, x[3], *p, f(); void g(void) { "),
        expression,
        S8("; }"),
    };
    String8 source = string_join_arena(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(parts), false);
    return c_ast_test_check(arguments, source, C_PREPROCESS_DIALECT_GNU17, C_AST_EXPRESSION_STATEMENT, true, expected, false);
}

#define c_ast_test_expect_expression(arguments, expression, expected) \
    c_ast_test_merge(&result, c_ast_test_check_expression((arguments), (expression), (expected)))

// Precedence, associativity, postfix and prefix forms, casts against
// parenthesized expressions, and `sizeof`.
BUSTER_GLOBAL_LOCAL UnitTestResult c_ast_test_expressions(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    c_ast_test_expect_expression(arguments, S8("a = b = c"),
                                 S8("(assign (identifier a) (assign (identifier b) (identifier c)))"));
    c_ast_test_expect_expression(arguments, S8("a - b - c"),
                                 S8("(subtract (subtract (identifier a) (identifier b)) (identifier c))"));
    c_ast_test_expect_expression(arguments, S8("a ? b : c ? d : e"),
                                 S8("(conditional (identifier a) (identifier b) (conditional (identifier c) (identifier d) (identifier e)))"));
    c_ast_test_expect_expression(arguments, S8("a ? b, c : d"),
                                 S8("(conditional (identifier a) (comma (identifier b) (identifier c)) (identifier d))"));
    c_ast_test_expect_expression(arguments, S8("a = b ? c : d"),
                                 S8("(assign (identifier a) (conditional (identifier b) (identifier c) (identifier d)))"));
    c_ast_test_expect_expression(arguments, S8("-x[1]"),
                                 S8("(negate (index (identifier x) (number 1)))"));
    c_ast_test_expect_expression(arguments, S8("*p++"),
                                 S8("(dereference (post_increment (identifier p)))"));
    c_ast_test_expect_expression(arguments, S8("!a++"),
                                 S8("(logical_not (post_increment (identifier a)))"));
    c_ast_test_expect_expression(arguments, S8("&*p"),
                                 S8("(address (dereference (identifier p)))"));
    c_ast_test_expect_expression(arguments, S8("(T)x"),
                                 S8("(cast (type_name (decl_specifiers (typedef_name T))) (identifier x))"));
    c_ast_test_expect_expression(arguments, S8("(T)(x)"),
                                 S8("(cast (type_name (decl_specifiers (typedef_name T))) (identifier x))"));
    c_ast_test_expect_expression(arguments, S8("(a)"),
                                 S8("(identifier a)"));
    c_ast_test_expect_expression(arguments, S8("sizeof (T)"),
                                 S8("(sizeof_type (type_name (decl_specifiers (typedef_name T))))"));
    c_ast_test_expect_expression(arguments, S8("sizeof a + 1"),
                                 S8("(add (sizeof_expression (identifier a)) (number 1))"));
    c_ast_test_expect_expression(arguments, S8("sizeof (T) * 2"),
                                 S8("(multiply (sizeof_type (type_name (decl_specifiers (typedef_name T)))) (number 2))"));
    c_ast_test_expect_expression(arguments, S8("sizeof (int){1}"),
                                 S8("(sizeof_expression (compound_literal (type_name (decl_specifiers (specifier_word int))) (initializer_list (number 1))))"));
    c_ast_test_expect_expression(arguments, S8("a + b * c"),
                                 S8("(add (identifier a) (multiply (identifier b) (identifier c)))"));
    c_ast_test_expect_expression(arguments, S8("a * b + c"),
                                 S8("(add (multiply (identifier a) (identifier b)) (identifier c))"));
    c_ast_test_expect_expression(arguments, S8("a < b == c"),
                                 S8("(equal (less (identifier a) (identifier b)) (identifier c))"));
    c_ast_test_expect_expression(arguments, S8("a && b || c"),
                                 S8("(logical_or (logical_and (identifier a) (identifier b)) (identifier c))"));
    c_ast_test_expect_expression(arguments, S8("a || b && c"),
                                 S8("(logical_or (identifier a) (logical_and (identifier b) (identifier c)))"));
    c_ast_test_expect_expression(arguments, S8("a | b ^ c & d"),
                                 S8("(bit_or (identifier a) (bit_xor (identifier b) (bit_and (identifier c) (identifier d))))"));
    c_ast_test_expect_expression(arguments, S8("a << b + c"),
                                 S8("(shift_left (identifier a) (add (identifier b) (identifier c)))"));
    c_ast_test_expect_expression(arguments, S8("a + b << c"),
                                 S8("(shift_left (add (identifier a) (identifier b)) (identifier c))"));
    c_ast_test_expect_expression(arguments, S8("a, b = c"),
                                 S8("(comma (identifier a) (assign (identifier b) (identifier c)))"));
    c_ast_test_expect_expression(arguments, S8("f(a, b)"),
                                 S8("(call (identifier f) (identifier a) (identifier b))"));
    c_ast_test_expect_expression(arguments, S8("f()"),
                                 S8("(call (identifier f))"));
    c_ast_test_expect_expression(arguments, S8("f(a)(b)"),
                                 S8("(call (call (identifier f) (identifier a)) (identifier b))"));
    c_ast_test_expect_expression(arguments, S8("(a, b)"),
                                 S8("(comma (identifier a) (identifier b))"));
    c_ast_test_expect_expression(arguments, S8("-(a + b)"),
                                 S8("(negate (add (identifier a) (identifier b)))"));
    c_ast_test_expect_expression(arguments, S8("a ?: b"),
                                 S8("(conditional_omitted (identifier a) (identifier b))"));
    c_ast_test_expect_expression(arguments, S8("(int)1.0"),
                                 S8("(cast (type_name (decl_specifiers (specifier_word int))) (number 1.0))"));
    c_ast_test_expect_expression(arguments, S8("(int *)p"),
                                 S8("(cast (type_name (decl_specifiers (specifier_word int)) (declarator_pointer)) (identifier p))"));
    c_ast_test_expect_expression(arguments, S8("(T){1}"),
                                 S8("(compound_literal (type_name (decl_specifiers (typedef_name T))) (initializer_list (number 1)))"));
    c_ast_test_expect_expression(arguments, S8("(int[]){1, 2}"),
                                 S8("(compound_literal (type_name (decl_specifiers (specifier_word int)) (declarator_array)) (initializer_list (number 1) (number 2)))"));
    c_ast_test_expect_expression(arguments, S8("__builtin_offsetof(T, a.b[2])"),
                                 S8("(call (identifier __builtin_offsetof) (type_name (decl_specifiers (typedef_name T))) (index (member b (identifier a)) (number 2)))"));
    c_ast_test_expect_expression(arguments, S8("_Generic(a, int: 1, default: 2)"),
                                 S8("(generic_selection (identifier a) (generic_association (type_name (decl_specifiers (specifier_word int))) (number 1)) (generic_default (number 2)))"));
    c_ast_test_expect_expression(arguments, S8("a++ + ++b"),
                                 S8("(add (post_increment (identifier a)) (pre_increment (identifier b)))"));
    c_ast_test_expect_expression(arguments, S8("- - a"),
                                 S8("(negate (negate (identifier a)))"));
    c_ast_test_expect_expression(arguments, S8("a = b += c"),
                                 S8("(assign (identifier a) (add_assign (identifier b) (identifier c)))"));
    c_ast_test_expect_expression(arguments, S8("x[a][b].c->d"),
                                 S8("(member_arrow d (member c (index (index (identifier x) (identifier a)) (identifier b))))"));
    c_ast_test_expect_expression(arguments, S8("sizeof(int) + sizeof a"),
                                 S8("(add (sizeof_type (type_name (decl_specifiers (specifier_word int)))) (sizeof_expression (identifier a)))"));
    c_ast_test_expect_expression(arguments, S8("(void)0"),
                                 S8("(cast (type_name (decl_specifiers (specifier_word void))) (number 0))"));
    c_ast_test_expect_expression(arguments, S8("a ? b : c = d"),
                                 S8("(assign (conditional (identifier a) (identifier b) (identifier c)) (identifier d))"));
    c_ast_test_expect_expression(arguments, S8("(*p)(1)"),
                                 S8("(call (dereference (identifier p)) (number 1))"));
    c_ast_test_expect_expression(arguments, S8("f(1)(2)"),
                                 S8("(call (call (identifier f) (number 1)) (number 2))"));
    c_ast_test_expect_expression(arguments, S8("(int[]){1}[0]"),
                                 S8("(index (compound_literal (type_name (decl_specifiers (specifier_word int)) (declarator_array)) (initializer_list (number 1))) (number 0))"));
    c_ast_test_expect_expression(arguments, S8("(int)-1"),
                                 S8("(cast (type_name (decl_specifiers (specifier_word int))) (negate (number 1)))"));
    c_ast_test_expect_expression(arguments, S8("(a)-1"),
                                 S8("(subtract (identifier a) (number 1))"));
    c_ast_test_expect_expression(arguments, S8("sizeof (a) - 1"),
                                 S8("(subtract (sizeof_expression (identifier a)) (number 1))"));
    c_ast_test_expect_expression(arguments, S8("(T)-1 + 2"),
                                 S8("(add (cast (type_name (decl_specifiers (typedef_name T))) (negate (number 1))) (number 2))"));
    c_ast_test_expect_expression(arguments, S8("(char)a << 3"),
                                 S8("(shift_left (cast (type_name (decl_specifiers (specifier_word char))) (identifier a)) (number 3))"));
    c_ast_test_expect_expression(arguments, S8("a ? (T)b : (c)"),
                                 S8("(conditional (identifier a) (cast (type_name (decl_specifiers (typedef_name T))) (identifier b)) (identifier c))"));
    c_ast_test_expect_expression(arguments, S8("(a ? b : c) = d"),
                                 S8("(assign (conditional (identifier a) (identifier b) (identifier c)) (identifier d))"));
    c_ast_test_expect_expression(arguments, S8("a, b, c"),
                                 S8("(comma (comma (identifier a) (identifier b)) (identifier c))"));
    c_ast_test_expect_expression(arguments, S8("x[a + 1]->p[2]"),
                                 S8("(index (member_arrow p (index (identifier x) (add (identifier a) (number 1)))) (number 2))"));
    c_ast_test_expect_expression(arguments, S8("\"s\" \"t\""),
                                 S8("(string \"s\" \"t\")"));
    c_ast_test_expect_expression(arguments, S8("'c' + 1"),
                                 S8("(add (character 'c') (number 1))"));
    c_ast_test_expect_expression(arguments, S8("1.5e3 * 0x10"),
                                 S8("(multiply (number 1.5e3) (number 0x10))"));
    c_ast_test_expect_expression(arguments, S8("!(a && b)"),
                                 S8("(logical_not (logical_and (identifier a) (identifier b)))"));
    c_ast_test_expect_expression(arguments, S8("~a & b"),
                                 S8("(bit_and (bit_not (identifier a)) (identifier b))"));
    c_ast_test_expect_expression(arguments, S8("++*p"),
                                 S8("(pre_increment (dereference (identifier p)))"));
    c_ast_test_expect_expression(arguments, S8("*&a"),
                                 S8("(dereference (address (identifier a)))"));
    c_ast_test_expect_expression(arguments, S8("(((a)))"),
                                 S8("(identifier a)"));
    c_ast_test_expect_expression(arguments, S8("f((a, b), c)"),
                                 S8("(call (identifier f) (comma (identifier a) (identifier b)) (identifier c))"));
    c_ast_test_expect_expression(arguments, S8("a ? b ? c : d : e"),
                                 S8("(conditional (identifier a) (conditional (identifier b) (identifier c) (identifier d)) (identifier e))"));
    return result;
}

// Declarators, typedef-name shadowing, tags, initializers, function
// definitions (prototype, K&R and implicit int), attributes and assembly.
BUSTER_GLOBAL_LOCAL UnitTestResult c_ast_test_declarations(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    c_ast_test_expect(arguments, S8("typedef int T; void f(void) { int T = 1; T * 2; }"),
                      S8("(translation_unit (declaration (decl_specifiers (specifier_word typedef) (specifier_word int)) (init_declarator (declarator_name T))) (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (declaration (decl_specifiers (specifier_word int)) (init_declarator (declarator_name T) (number 1))) (expression_statement (multiply (identifier T) (number 2))))))"));
    c_ast_test_expect(arguments, S8("typedef int T; void f(void) { T: ; T * x; }"),
                      S8("(translation_unit (declaration (decl_specifiers (specifier_word typedef) (specifier_word int)) (init_declarator (declarator_name T))) (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (labeled T (null_statement)) (declaration (decl_specifiers (typedef_name T)) (init_declarator (declarator_pointer (declarator_name x)))))))"));
    c_ast_test_expect(arguments, S8("typedef int T; void f(void) { T * x; }"),
                      S8("(translation_unit (declaration (decl_specifiers (specifier_word typedef) (specifier_word int)) (init_declarator (declarator_name T))) (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (declaration (decl_specifiers (typedef_name T)) (init_declarator (declarator_pointer (declarator_name x)))))))"));
    c_ast_test_expect(arguments, S8("typedef int T; void (*f(int a))(T T) { T * p; }"),
                      S8("(translation_unit (declaration (decl_specifiers (specifier_word typedef) (specifier_word int)) (init_declarator (declarator_name T))) (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_pointer (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word int)) (declarator_name a))))) (parameter_list (parameter (decl_specifiers (typedef_name T)) (declarator_name T)))) (compound_statement (declaration (decl_specifiers (typedef_name T)) (init_declarator (declarator_pointer (declarator_name p)))))))"));
    c_ast_test_expect(arguments, S8("typedef int T; void f(T T) { T * 2; }"),
                      S8("(translation_unit (declaration (decl_specifiers (specifier_word typedef) (specifier_word int)) (init_declarator (declarator_name T))) (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (typedef_name T)) (declarator_name T)))) (compound_statement (expression_statement (multiply (identifier T) (number 2))))))"));
    c_ast_test_expect(arguments, S8("typedef int T; void f(void) { { int T; { T * 3; } } T * x; }"),
                      S8("(translation_unit (declaration (decl_specifiers (specifier_word typedef) (specifier_word int)) (init_declarator (declarator_name T))) (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (compound_statement (declaration (decl_specifiers (specifier_word int)) (init_declarator (declarator_name T))) (compound_statement (expression_statement (multiply (identifier T) (number 3))))) (declaration (decl_specifiers (typedef_name T)) (init_declarator (declarator_pointer (declarator_name x)))))))"));
    c_ast_test_expect(arguments, S8("typedef int T; void f(void) { for (int T = 0; T < 3; T++) T * 2; T * y; }"),
                      S8("(translation_unit (declaration (decl_specifiers (specifier_word typedef) (specifier_word int)) (init_declarator (declarator_name T))) (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (for (declaration (decl_specifiers (specifier_word int)) (init_declarator (declarator_name T) (number 0))) (less (identifier T) (number 3)) (post_increment (identifier T)) (expression_statement (multiply (identifier T) (number 2)))) (declaration (decl_specifiers (typedef_name T)) (init_declarator (declarator_pointer (declarator_name y)))))))"));
    c_ast_test_expect(arguments, S8("typedef int T; int g(int (*h)(T x), T y) { return y; }"),
                      S8("(translation_unit (declaration (decl_specifiers (specifier_word typedef) (specifier_word int)) (init_declarator (declarator_name T))) (function_definition (decl_specifiers (specifier_word int)) (declarator_function (declarator_name g) (parameter_list (parameter (decl_specifiers (specifier_word int)) (declarator_function (declarator_pointer (declarator_name h)) (parameter_list (parameter (decl_specifiers (typedef_name T)) (declarator_name x))))) (parameter (decl_specifiers (typedef_name T)) (declarator_name y)))) (compound_statement (return (identifier y)))))"));
    // glibc's <bits/floatn.h> for the compiler identity this preprocessor
    // reports: `__float128` is a builtin type word, `_Float128` a typedef name.
    c_ast_test_expect(arguments, S8("typedef __float128 _Float128; _Float128 x;"),
                      S8("(translation_unit (declaration (decl_specifiers (specifier_word typedef) (specifier_word __float128)) (init_declarator (declarator_name _Float128))) (declaration (decl_specifiers (typedef_name _Float128)) (init_declarator (declarator_name x))))"));
    c_ast_test_expect(arguments, S8("int x;"),
                      S8("(translation_unit (declaration (decl_specifiers (specifier_word int)) (init_declarator (declarator_name x))))"));
    c_ast_test_expect(arguments, S8("int *a[3];"),
                      S8("(translation_unit (declaration (decl_specifiers (specifier_word int)) (init_declarator (declarator_pointer (declarator_array (declarator_name a) (number 3))))))"));
    c_ast_test_expect(arguments, S8("int (*p)[3];"),
                      S8("(translation_unit (declaration (decl_specifiers (specifier_word int)) (init_declarator (declarator_array (declarator_pointer (declarator_name p)) (number 3)))))"));
    c_ast_test_expect(arguments, S8("int (*(*f)(int))[2];"),
                      S8("(translation_unit (declaration (decl_specifiers (specifier_word int)) (init_declarator (declarator_array (declarator_pointer (declarator_function (declarator_pointer (declarator_name f)) (parameter_list (parameter (decl_specifiers (specifier_word int)))))) (number 2)))))"));
    c_ast_test_expect(arguments, S8("void f(int, char *);"),
                      S8("(translation_unit (declaration (decl_specifiers (specifier_word void)) (init_declarator (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word int))) (parameter (decl_specifiers (specifier_word char)) (declarator_pointer)))))))"));
    c_ast_test_expect(arguments, S8("void f(int a[static 3], int b[*], int c[const 4], int d[]);"),
                      S8("(translation_unit (declaration (decl_specifiers (specifier_word void)) (init_declarator (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word int)) (declarator_array static (declarator_name a) (number 3))) (parameter (decl_specifiers (specifier_word int)) (declarator_array * (declarator_name b))) (parameter (decl_specifiers (specifier_word int)) (declarator_array const (declarator_name c) (number 4))) (parameter (decl_specifiers (specifier_word int)) (declarator_array (declarator_name d))))))))"));
    c_ast_test_expect(arguments, S8("int * const * p;"),
                      S8("(translation_unit (declaration (decl_specifiers (specifier_word int)) (init_declarator (declarator_pointer const (declarator_pointer (declarator_name p))))))"));
    c_ast_test_expect(arguments, S8("char *const volatile *restrict q;"),
                      S8("(translation_unit (declaration (decl_specifiers (specifier_word char)) (init_declarator (declarator_pointer const volatile (declarator_pointer restrict (declarator_name q))))))"));
    c_ast_test_expect(arguments, S8("int g(void) { return (int (*)(void))0; }"),
                      S8("(translation_unit (function_definition (decl_specifiers (specifier_word int)) (declarator_function (declarator_name g) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (return (cast (type_name (decl_specifiers (specifier_word int)) (declarator_function (declarator_pointer) (parameter_list (parameter (decl_specifiers (specifier_word void)))))) (number 0))))))"));
    c_ast_test_expect(arguments, S8("int g(void) { return sizeof(int (*)(int, char *)); }"),
                      S8("(translation_unit (function_definition (decl_specifiers (specifier_word int)) (declarator_function (declarator_name g) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (return (sizeof_type (type_name (decl_specifiers (specifier_word int)) (declarator_function (declarator_pointer) (parameter_list (parameter (decl_specifiers (specifier_word int))) (parameter (decl_specifiers (specifier_word char)) (declarator_pointer))))))))))"));
    c_ast_test_expect(arguments, S8("int (*fp)(int (*cb)(int x), char *s) = 0;"),
                      S8("(translation_unit (declaration (decl_specifiers (specifier_word int)) (init_declarator (declarator_function (declarator_pointer (declarator_name fp)) (parameter_list (parameter (decl_specifiers (specifier_word int)) (declarator_function (declarator_pointer (declarator_name cb)) (parameter_list (parameter (decl_specifiers (specifier_word int)) (declarator_name x))))) (parameter (decl_specifiers (specifier_word char)) (declarator_pointer (declarator_name s))))) (number 0))))"));
    c_ast_test_expect(arguments, S8("int f();"),
                      S8("(translation_unit (declaration (decl_specifiers (specifier_word int)) (init_declarator (declarator_function (declarator_name f) (parameter_list)))))"));
    c_ast_test_expect(arguments, S8("int f(void) __attribute__((nothrow)), g(void);"),
                      S8("(translation_unit (declaration (decl_specifiers (specifier_word int)) (init_declarator (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (attribute_list (attribute_gnu (attribute nothrow)))) (init_declarator (declarator_function (declarator_name g) (parameter_list (parameter (decl_specifiers (specifier_word void))))))))"));
    c_ast_test_expect(arguments, S8("int f(void) asm(\"g\");"),
                      S8("(translation_unit (declaration (decl_specifiers (specifier_word int)) (init_declarator (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (asm_label (string \"g\")))))"));
    c_ast_test_expect(arguments, S8("int f(a, b) int a; char *b; { return a; }"),
                      S8("(translation_unit (function_definition (decl_specifiers (specifier_word int)) (declarator_function (declarator_name f) (identifier_list (declarator_name a) (declarator_name b))) (declaration (decl_specifiers (specifier_word int)) (init_declarator (declarator_name a))) (declaration (decl_specifiers (specifier_word char)) (init_declarator (declarator_pointer (declarator_name b)))) (compound_statement (return (identifier a)))))"));
    c_ast_test_expect(arguments, S8("main() { return 0; }"),
                      S8("(translation_unit (function_definition (decl_specifiers) (declarator_function (declarator_name main) (parameter_list)) (compound_statement (return (number 0)))))"));
    c_ast_test_expect(arguments, S8("struct S { int a; unsigned b : 3; int : 2; struct { int x; }; } s;"),
                      S8("(translation_unit (declaration (decl_specifiers (struct_specifier (tag_name S) (member_list (member_declaration (decl_specifiers (specifier_word int)) (member_declarator (declarator_name a))) (member_declaration (decl_specifiers (specifier_word unsigned)) (member_declarator (declarator_name b) (number 3))) (member_declaration (decl_specifiers (specifier_word int)) (member_declarator (number 2))) (member_declaration (decl_specifiers (struct_specifier (member_list (member_declaration (decl_specifiers (specifier_word int)) (member_declarator (declarator_name x)))))))))) (init_declarator (declarator_name s))))"));
    c_ast_test_expect(arguments, S8("union U { int a; float b; };"),
                      S8("(translation_unit (declaration (decl_specifiers (union_specifier (tag_name U) (member_list (member_declaration (decl_specifiers (specifier_word int)) (member_declarator (declarator_name a))) (member_declaration (decl_specifiers (specifier_word float)) (member_declarator (declarator_name b))))))))"));
    c_ast_test_expect(arguments, S8("struct S;"),
                      S8("(translation_unit (declaration (decl_specifiers (struct_specifier (tag_name S)))))"));
    c_ast_test_expect(arguments, S8("struct S { int a __attribute__((aligned(8))); } __attribute__((packed));"),
                      S8("(translation_unit (declaration (decl_specifiers (struct_specifier (tag_name S) (member_list (member_declaration (decl_specifiers (specifier_word int)) (member_declarator (declarator_name a) (attribute_list (attribute_gnu (attribute aligned (number 8))))))) (attribute_list (attribute_gnu (attribute packed)))))))"));
    c_ast_test_expect(arguments, S8("enum E { A, B = 2, C } e;"),
                      S8("(translation_unit (declaration (decl_specifiers (enum_specifier (tag_name E) (enumerator_list (enumerator A) (enumerator B (number 2)) (enumerator C)))) (init_declarator (declarator_name e))))"));
    c_ast_test_expect(arguments, S8("enum F : unsigned char { X, Y, };"),
                      S8("(translation_unit (declaration (decl_specifiers (enum_specifier (tag_name F) (type_name (decl_specifiers (specifier_word unsigned) (specifier_word char))) (enumerator_list (enumerator X) (enumerator Y))))))"));
    c_ast_test_expect(arguments, S8("int a[] = {1, 2, [5] = 3, [1 ... 2] = 4};"),
                      S8("(translation_unit (declaration (decl_specifiers (specifier_word int)) (init_declarator (declarator_array (declarator_name a)) (initializer_list (number 1) (number 2) (designation (designator_index (number 5)) (number 3)) (designation (designator_range (number 1) (number 2)) (number 4))))))"));
    c_ast_test_expect(arguments, S8("struct P { int x, y; } p = { .x = 1, .y = 2 };"),
                      S8("(translation_unit (declaration (decl_specifiers (struct_specifier (tag_name P) (member_list (member_declaration (decl_specifiers (specifier_word int)) (member_declarator (declarator_name x)) (member_declarator (declarator_name y)))))) (init_declarator (declarator_name p) (initializer_list (designation (designator_member x) (number 1)) (designation (designator_member y) (number 2))))))"));
    c_ast_test_expect(arguments, S8("struct Q { struct P p; int v[2]; } q = { { 1, 2 }, { 3, 4 } };"),
                      S8("(translation_unit (declaration (decl_specifiers (struct_specifier (tag_name Q) (member_list (member_declaration (decl_specifiers (struct_specifier (tag_name P))) (member_declarator (declarator_name p))) (member_declaration (decl_specifiers (specifier_word int)) (member_declarator (declarator_array (declarator_name v) (number 2))))))) (init_declarator (declarator_name q) (initializer_list (initializer_list (number 1) (number 2)) (initializer_list (number 3) (number 4))))))"));
    c_ast_test_expect(arguments, S8("int a[2][2] = { [1] = { [0] = 1 } };"),
                      S8("(translation_unit (declaration (decl_specifiers (specifier_word int)) (init_declarator (declarator_array (declarator_array (declarator_name a) (number 2)) (number 2)) (initializer_list (designation (designator_index (number 1)) (initializer_list (designation (designator_index (number 0)) (number 1))))))))"));
    c_ast_test_expect(arguments, S8("void f(void) { int x = 1; if (x) x = 2; else x = 3; while (x) { x--; } do x++; while (x < 3); }"),
                      S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (declaration (decl_specifiers (specifier_word int)) (init_declarator (declarator_name x) (number 1))) (if_else (identifier x) (expression_statement (assign (identifier x) (number 2))) (expression_statement (assign (identifier x) (number 3)))) (while (identifier x) (compound_statement (expression_statement (post_decrement (identifier x))))) (do_while (expression_statement (post_increment (identifier x))) (less (identifier x) (number 3))))))"));
    c_ast_test_expect(arguments, S8("void f(void) { for (int i = 0, j = 1; i < j; i++, j--) ; for (;;) break; }"),
                      S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (for (declaration (decl_specifiers (specifier_word int)) (init_declarator (declarator_name i) (number 0)) (init_declarator (declarator_name j) (number 1))) (less (identifier i) (identifier j)) (comma (post_increment (identifier i)) (post_decrement (identifier j))) (null_statement)) (for (break)))))"));
    c_ast_test_expect(arguments, S8("void f(int x) { switch (x) { case 1: break; case 2 ... 3: x = 0; default: ; } goto L; L: return; }"),
                      S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word int)) (declarator_name x)))) (compound_statement (switch (identifier x) (compound_statement (case (number 1) (break)) (case_range (number 2) (number 3) (expression_statement (assign (identifier x) (number 0)))) (default (null_statement)))) (goto L) (labeled L (return)))))"));
    c_ast_test_expect(arguments, S8("void f(void) { if (a) if (b) x; else y; }"),
                      S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (if (identifier a) (if_else (identifier b) (expression_statement (identifier x)) (expression_statement (identifier y)))))))"));
    c_ast_test_expect(arguments, S8("static int x __attribute__((unused)) = 3;"),
                      S8("(translation_unit (declaration (decl_specifiers (specifier_word static) (specifier_word int)) (init_declarator (declarator_name x) (attribute_list (attribute_gnu (attribute unused))) (number 3))))"));
    c_ast_test_expect(arguments, S8("__attribute__((noreturn)) void die(void);"),
                      S8("(translation_unit (declaration (decl_specifiers (attribute_list (attribute_gnu (attribute noreturn))) (specifier_word void)) (init_declarator (declarator_function (declarator_name die) (parameter_list (parameter (decl_specifiers (specifier_word void))))))))"));
    c_ast_test_expect(arguments, S8("void f(void) { asm volatile (\"nop\" : \"=r\"(x) : \"r\"(y), [n] \"i\"(1) : \"memory\"); }"),
                      S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (asm volatile (string \"nop\") (asm_outputs (asm_operand (string \"=r\") (identifier x))) (asm_inputs (asm_operand (string \"r\") (identifier y)) (asm_operand (asm_symbolic_name n) (string \"i\") (number 1))) (asm_clobbers (string \"memory\"))))))"));
    c_ast_test_expect(arguments, S8("void f(void) { asm goto (\"jmp %l0\" : : : : out); out: ; }"),
                      S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (asm goto (string \"jmp %l0\") (asm_outputs) (asm_inputs) (asm_clobbers) (asm_labels (identifier out))) (labeled out (null_statement)))))"));
    c_ast_test_expect(arguments, S8("_Static_assert(1, \"ok\");"),
                      S8("(translation_unit (static_assert (number 1) (string \"ok\")))"));
    c_ast_test_expect(arguments, S8("asm(\"nop\");"),
                      S8("(translation_unit (asm_top_level (string \"nop\")))"));
    c_ast_test_expect(arguments, S8("void f(void) { int *p = (int[]){1, 2}; }"),
                      S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (declaration (decl_specifiers (specifier_word int)) (init_declarator (declarator_pointer (declarator_name p)) (compound_literal (type_name (decl_specifiers (specifier_word int)) (declarator_array)) (initializer_list (number 1) (number 2))))))))"));
    c_ast_test_expect(arguments, S8("void f(void) { ({ int y = 1; y; }); }"),
                      S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (statement_expression (compound_statement (declaration (decl_specifiers (specifier_word int)) (init_declarator (declarator_name y) (number 1))) (expression_statement (identifier y))))))))"));
    c_ast_test_expect(arguments, S8("void f(int a, int b) { void *l = &&out; goto *l; out: ; }"),
                      S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word int)) (declarator_name a)) (parameter (decl_specifiers (specifier_word int)) (declarator_name b)))) (compound_statement (declaration (decl_specifiers (specifier_word void)) (init_declarator (declarator_pointer (declarator_name l)) (label_address out))) (goto_computed (identifier l)) (labeled out (null_statement)))))"));
    c_ast_test_expect(arguments, S8("typeof(int) a; typeof(a) b; _Atomic(int) c; _Atomic int d; _Alignas(8) int e; __auto_type f = 1; unsigned __int128 g;"),
                      S8("(translation_unit (declaration (decl_specifiers (typeof (type_name (decl_specifiers (specifier_word int))))) (init_declarator (declarator_name a))) (declaration (decl_specifiers (typeof (identifier a))) (init_declarator (declarator_name b))) (declaration (decl_specifiers (atomic_specifier (type_name (decl_specifiers (specifier_word int))))) (init_declarator (declarator_name c))) (declaration (decl_specifiers (specifier_word _Atomic) (specifier_word int)) (init_declarator (declarator_name d))) (declaration (decl_specifiers (alignas (number 8)) (specifier_word int)) (init_declarator (declarator_name e))) (declaration (decl_specifiers (specifier_word __auto_type)) (init_declarator (declarator_name f) (number 1))) (declaration (decl_specifiers (specifier_word unsigned) (specifier_word __int128)) (init_declarator (declarator_name g))))"));
    c_ast_test_expect(arguments, S8("void f(void) { __extension__ int x; __extension__ ({ 1; }); __extension__ x = 2; }"),
                      S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (declaration (decl_specifiers (specifier_word __extension__) (specifier_word int)) (init_declarator (declarator_name x))) (expression_statement (extension (statement_expression (compound_statement (expression_statement (number 1)))))) (expression_statement (assign (extension (identifier x)) (number 2))))))"));
    c_ast_test_expect(arguments, S8("[[nodiscard]] int f(void);"),
                      S8("(translation_unit (declaration (decl_specifiers (attribute_list (attribute_standard (attribute nodiscard))) (specifier_word int)) (init_declarator (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))))))"));
    c_ast_test_expect(arguments, S8("int x [[deprecated]];"),
                      S8("(translation_unit (declaration (decl_specifiers (specifier_word int)) (init_declarator (declarator_name x) (attribute_list (attribute_standard (attribute deprecated))))))"));
    c_ast_test_expect(arguments, S8("void f(void) { [[fallthrough]]; __attribute__((fallthrough)); }"),
                      S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (attribute_statement (attribute_list (attribute_standard (attribute fallthrough)))) (attribute_statement (attribute_list (attribute_gnu (attribute fallthrough)))))))"));
    c_ast_test_expect(arguments, S8("void f(void) { for ([[maybe_unused]] int i = 0; i < 1; i++) ; }"),
                      S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (for (declaration (decl_specifiers (attribute_list (attribute_standard (attribute maybe_unused))) (specifier_word int)) (init_declarator (declarator_name i) (number 0))) (less (identifier i) (number 1)) (post_increment (identifier i)) (null_statement)))))"));
    c_ast_test_expect(arguments, S8("int first, __attribute__((aligned(32))) second;"),
                      S8("(translation_unit (declaration (decl_specifiers (specifier_word int)) (init_declarator (declarator_name first)) (init_declarator (attribute_list (attribute_gnu (attribute aligned (number 32)))) (declarator_name second))))"));
    c_ast_test_expect(arguments, S8("int f(void) { int true = 1; return true; }"),
                      S8("(translation_unit (function_definition (decl_specifiers (specifier_word int)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (declaration (decl_specifiers (specifier_word int)) (init_declarator (declarator_name true) (number 1))) (return (identifier true)))))"));
    c_ast_test_expect(arguments, S8("int a<:3:>; void f(void) <% a<:0:> = 1; %>"),
                      S8("(translation_unit (declaration (decl_specifiers (specifier_word int)) (init_declarator (declarator_array (declarator_name a) (number 3)))) (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (assign (index (identifier a) (number 0)) (number 1))))))"));
    c_ast_test_expect(arguments, S8("void f(void) { int x; x = 1; } int y;"),
                      S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (declaration (decl_specifiers (specifier_word int)) (init_declarator (declarator_name x))) (expression_statement (assign (identifier x) (number 1))))) (declaration (decl_specifiers (specifier_word int)) (init_declarator (declarator_name y))))"));
    c_ast_test_expect(arguments, S8("void f(void) { goto *&&l; l:; }"),
                      S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (goto_computed (label_address l)) (labeled l (null_statement)))))"));
    c_ast_test_expect(arguments, S8(""),
                      S8("(translation_unit)"));
    c_ast_test_expect(arguments, S8("  /* comment */ \n"),
                      S8("(translation_unit)"));
    c_ast_test_expect(arguments, S8("int printf(const char *fmt, ...);"),
                      S8("(translation_unit (declaration (decl_specifiers (specifier_word int)) (init_declarator (declarator_function (declarator_name printf) (parameter_list_variadic (parameter (decl_specifiers (specifier_word const) (specifier_word char)) (declarator_pointer (declarator_name fmt))))))))"));
    c_ast_test_expect(arguments, S8("void f(void) { int g(int x) { return x; } g(1); }"),
                      S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (function_definition (decl_specifiers (specifier_word int)) (declarator_function (declarator_name g) (parameter_list (parameter (decl_specifiers (specifier_word int)) (declarator_name x)))) (compound_statement (return (identifier x)))) (expression_statement (call (identifier g) (number 1))))))"));
    c_ast_test_expect(arguments, S8("typedef int T; struct R { T T; int (*cb)(T); };"),
                      S8("(translation_unit (declaration (decl_specifiers (specifier_word typedef) (specifier_word int)) (init_declarator (declarator_name T))) (declaration (decl_specifiers (struct_specifier (tag_name R) (member_list (member_declaration (decl_specifiers (typedef_name T)) (member_declarator (declarator_name T))) (member_declaration (decl_specifiers (specifier_word int)) (member_declarator (declarator_function (declarator_pointer (declarator_name cb)) (parameter_list (parameter (decl_specifiers (typedef_name T))))))))))))"));
    c_ast_test_expect(arguments, S8("enum E { A __attribute__((deprecated)) = 1, B [[deprecated(\"x\")]] };"),
                      S8("(translation_unit (declaration (decl_specifiers (enum_specifier (tag_name E) (enumerator_list (enumerator A (attribute_list (attribute_gnu (attribute deprecated))) (number 1)) (enumerator B (attribute_list (attribute_standard (attribute deprecated (string \"x\"))))))))))"));
    c_ast_test_expect(arguments, S8("_BitInt(8) bi; unsigned _BitInt(16) ub;"),
                      S8("(translation_unit (declaration (decl_specifiers (bitint (number 8))) (init_declarator (declarator_name bi))) (declaration (decl_specifiers (specifier_word unsigned) (bitint (number 16))) (init_declarator (declarator_name ub))))"));
    c_ast_test_expect(arguments, S8("int x = ({ int t = 1; t; }) + 1;"),
                      S8("(translation_unit (declaration (decl_specifiers (specifier_word int)) (init_declarator (declarator_name x) (add (statement_expression (compound_statement (declaration (decl_specifiers (specifier_word int)) (init_declarator (declarator_name t) (number 1))) (expression_statement (identifier t)))) (number 1)))))"));
    c_ast_test_expect(arguments, S8("void f(int n, int a[n]); void g(void) { int (*p)[3]; }"),
                      S8("(translation_unit (declaration (decl_specifiers (specifier_word void)) (init_declarator (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word int)) (declarator_name n)) (parameter (decl_specifiers (specifier_word int)) (declarator_array (declarator_name a) (identifier n))))))) (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name g) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (declaration (decl_specifiers (specifier_word int)) (init_declarator (declarator_array (declarator_pointer (declarator_name p)) (number 3)))))))"));
    c_ast_test_expect(arguments, S8("register int r asm(\"eax\"); static const volatile unsigned long long int v = 0;"),
                      S8("(translation_unit (declaration (decl_specifiers (specifier_word register) (specifier_word int)) (init_declarator (declarator_name r) (asm_label (string \"eax\")))) (declaration (decl_specifiers (specifier_word static) (specifier_word const) (specifier_word volatile) (specifier_word unsigned) (specifier_word long) (specifier_word long) (specifier_word int)) (init_declarator (declarator_name v) (number 0))))"));
    c_ast_test_expect(arguments, S8("int a = sizeof(int[3][4]); char s[] = \"ab\" \"cd\" L\"ef\";"),
                      S8("(translation_unit (declaration (decl_specifiers (specifier_word int)) (init_declarator (declarator_name a) (sizeof_type (type_name (decl_specifiers (specifier_word int)) (declarator_array (declarator_array (number 3)) (number 4)))))) (declaration (decl_specifiers (specifier_word char)) (init_declarator (declarator_array (declarator_name s)) (string \"ab\" \"cd\" L\"ef\"))))"));
    c_ast_test_expect(arguments, S8("void f(void) { __builtin_expect(x, 1); __builtin_va_arg(ap, struct S *); }"),
                      S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (call (identifier __builtin_expect) (identifier x) (number 1))) (expression_statement (call (identifier __builtin_va_arg) (identifier ap) (type_name (decl_specifiers (struct_specifier (tag_name S))) (declarator_pointer)))))))"));
    c_ast_test_expect(arguments, S8("union { struct { int a, b; }; long l; } u;"),
                      S8("(translation_unit (declaration (decl_specifiers (union_specifier (member_list (member_declaration (decl_specifiers (struct_specifier (member_list (member_declaration (decl_specifiers (specifier_word int)) (member_declarator (declarator_name a)) (member_declarator (declarator_name b))))))) (member_declaration (decl_specifiers (specifier_word long)) (member_declarator (declarator_name l)))))) (init_declarator (declarator_name u))))"));
    c_ast_test_expect(arguments, S8("struct { int a; ; } s;"),
                      S8("(translation_unit (declaration (decl_specifiers (struct_specifier (member_list (member_declaration (decl_specifiers (specifier_word int)) (member_declarator (declarator_name a))) (empty_declaration)))) (init_declarator (declarator_name s))))"));
    c_ast_test_expect(arguments, S8("[[gnu::always_inline, clang::annotate(\"x\", 1)]] int f(void);"),
                      S8("(translation_unit (declaration (decl_specifiers (attribute_list (attribute_standard (attribute_scoped always_inline (attribute_namespace gnu)) (attribute_scoped annotate (attribute_namespace clang) (string \"x\") (number 1)))) (specifier_word int)) (init_declarator (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))))))"));
    c_ast_test_expect(arguments, S8("__declspec(dllexport) int g(void); __attribute__(()) int h; [[]] int i;"),
                      S8("(translation_unit (declaration (decl_specifiers (attribute_list (attribute_declspec (attribute dllexport))) (specifier_word int)) (init_declarator (declarator_function (declarator_name g) (parameter_list (parameter (decl_specifiers (specifier_word void))))))) (declaration (decl_specifiers (attribute_list (attribute_gnu)) (specifier_word int)) (init_declarator (declarator_name h))) (declaration (decl_specifiers (attribute_list (attribute_standard)) (specifier_word int)) (init_declarator (declarator_name i))))"));
    c_ast_test_expect(arguments, S8("int f(const char *, ...) __attribute__((format(printf, 1, 2), __nonnull__(1)));"),
                      S8("(translation_unit (declaration (decl_specifiers (specifier_word int)) (init_declarator (declarator_function (declarator_name f) (parameter_list_variadic (parameter (decl_specifiers (specifier_word const) (specifier_word char)) (declarator_pointer)))) (attribute_list (attribute_gnu (attribute format (identifier printf) (number 1) (number 2)) (attribute __nonnull__ (number 1)))))))"));
    c_ast_test_expect(arguments, S8("void f(int * __attribute__((unused)) p, int (__attribute__((unused)) *q)(void));"),
                      S8("(translation_unit (declaration (decl_specifiers (specifier_word void)) (init_declarator (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word int)) (declarator_pointer (attribute_list (attribute_gnu (attribute unused))) (declarator_name p))) (parameter (decl_specifiers (specifier_word int)) (declarator_function (declarator_pointer (attribute_list (attribute_gnu (attribute unused))) (declarator_name q)) (parameter_list (parameter (decl_specifiers (specifier_word void)))))))))))"));
    c_ast_test_expect(arguments, S8("int x __attribute__((aligned(__alignof__(long double))));"),
                      S8("(translation_unit (declaration (decl_specifiers (specifier_word int)) (init_declarator (declarator_name x) (attribute_list (attribute_gnu (attribute aligned (alignof_type (type_name (decl_specifiers (specifier_word long) (specifier_word double))))))))))"));
    c_ast_test_expect(arguments, S8("int y __attribute__((vector_size(sizeof(int) * 4)));"),
                      S8("(translation_unit (declaration (decl_specifiers (specifier_word int)) (init_declarator (declarator_name y) (attribute_list (attribute_gnu (attribute vector_size (multiply (sizeof_type (type_name (decl_specifiers (specifier_word int)))) (number 4))))))))"));
    return result;
}

// C23 keywords and forms against the same words as plain identifiers in the
// earlier dialects.
BUSTER_GLOBAL_LOCAL UnitTestResult c_ast_test_dialects(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    c_ast_test_expect_dialect(arguments, S8("enum E : int { A };"), C_PREPROCESS_DIALECT_C23,
                              S8("(translation_unit (declaration (decl_specifiers (enum_specifier (tag_name E) (type_name (decl_specifiers (specifier_word int))) (enumerator_list (enumerator A))))))"));
    c_ast_test_expect_dialect(arguments, S8("constexpr int x = 1;"), C_PREPROCESS_DIALECT_C23,
                              S8("(translation_unit (declaration (decl_specifiers (specifier_word constexpr) (specifier_word int)) (init_declarator (declarator_name x) (number 1))))"));
    c_ast_test_expect_dialect(arguments, S8("void f(void) { auto y = 1; int *p = nullptr; _Bool b = true; typeof_unqual(y) z; static_assert(1); alignas(8) int w; }"), C_PREPROCESS_DIALECT_C23,
                              S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (declaration (decl_specifiers (specifier_word auto)) (init_declarator (declarator_name y) (number 1))) (declaration (decl_specifiers (specifier_word int)) (init_declarator (declarator_pointer (declarator_name p)) (nullptr))) (declaration (decl_specifiers (specifier_word _Bool)) (init_declarator (declarator_name b) (boolean_constant true))) (declaration (decl_specifiers (typeof_unqual (identifier y))) (init_declarator (declarator_name z))) (static_assert (number 1)) (declaration (decl_specifiers (alignas (number 8)) (specifier_word int)) (init_declarator (declarator_name w))))))"));
    c_ast_test_expect_dialect(arguments, S8("thread_local int t;"), C_PREPROCESS_DIALECT_C23,
                              S8("(translation_unit (declaration (decl_specifiers (specifier_word _Thread_local) (specifier_word int)) (init_declarator (declarator_name t))))"));
    c_ast_test_expect_dialect(arguments, S8("void f(void) { switch (1) { case 1: } L: }"), C_PREPROCESS_DIALECT_C23,
                              S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (switch (number 1) (compound_statement (case (number 1)))) (labeled L))))"));
    c_ast_test_expect_dialect(arguments, S8("int true = 1, nullptr = 2, constexpr = 3;"), C_PREPROCESS_DIALECT_C99,
                              S8("(translation_unit (declaration (decl_specifiers (specifier_word int)) (init_declarator (declarator_name true) (number 1)) (init_declarator (declarator_name nullptr) (number 2)) (init_declarator (declarator_name constexpr) (number 3))))"));
    return result;
}

// The first syntax error stops the build with one diagnostic and no tree.
BUSTER_GLOBAL_LOCAL UnitTestResult c_ast_test_failures(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    c_ast_test_expect_failure(arguments, S8("int x"));
    // A typedef name is never an expression operand; an offsetof member
    // designator may still share a typedef's spelling.
    c_ast_test_expect_failure(arguments, S8("typedef int T; int y = T;"));
    c_ast_test_expect_failure(arguments, S8("typedef int T; int y = 1 + T;"));
    c_ast_test_expect_failure(arguments, S8("typedef int T; void f(void) { sizeof T; }"));
    c_ast_test_expect_failure(arguments, S8("typedef int T; struct S { int a; }; int y = __builtin_offsetof(struct S, a[T]);"));
    c_ast_test_expect(arguments, S8("typedef int T; struct S { int T; }; int y = __builtin_offsetof(struct S, T);"),
                      S8("(translation_unit (declaration (decl_specifiers (specifier_word typedef) (specifier_word int)) (init_declarator (declarator_name T))) (declaration (decl_specifiers (struct_specifier (tag_name S) (member_list (member_declaration (decl_specifiers (specifier_word int)) (member_declarator (declarator_name T))))))) (declaration (decl_specifiers (specifier_word int)) (init_declarator (declarator_name y) (call (identifier __builtin_offsetof) (type_name (decl_specifiers (struct_specifier (tag_name S)))) (identifier T)))))"));
    c_ast_test_expect(arguments, S8("typedef int T; struct S { struct { int a; } T; }; int y = __builtin_offsetof(struct S, T.a);"),
                      S8("(translation_unit (declaration (decl_specifiers (specifier_word typedef) (specifier_word int)) (init_declarator (declarator_name T))) (declaration (decl_specifiers (struct_specifier (tag_name S) (member_list (member_declaration (decl_specifiers (struct_specifier (member_list (member_declaration (decl_specifiers (specifier_word int)) (member_declarator (declarator_name a)))))) (member_declarator (declarator_name T))))))) (declaration (decl_specifiers (specifier_word int)) (init_declarator (declarator_name y) (call (identifier __builtin_offsetof) (type_name (decl_specifiers (struct_specifier (tag_name S)))) (member a (identifier T))))))"));
    // asm operands are comma-separated, with no trailing comma.
    c_ast_test_expect_failure(arguments, S8("void f(int x, int y) { asm(\"\" : \"=r\"(x) : \"r\"(y),); }"));
    c_ast_test_expect_failure(arguments, S8("int f("));
    c_ast_test_expect_failure(arguments, S8("int x = ;"));
    c_ast_test_expect_failure(arguments, S8("void f(void) { if (1 }"));
    c_ast_test_expect_failure(arguments, S8("struct S {"));
    c_ast_test_expect_failure(arguments, S8("int a[3"));
    c_ast_test_expect_failure(arguments, S8("void f(void) { return 1 +; }"));
    c_ast_test_expect_failure(arguments, S8("}"));
    c_ast_test_expect_failure(arguments, S8("void f(void) { x = (1; }"));
    c_ast_test_expect_failure(arguments, S8("enum { A, B"));
    c_ast_test_expect_failure(arguments, S8("int (*p;"));
    c_ast_test_expect_failure(arguments, S8("void f(void) { for (int i = 0; i < 3 i++) ; }"));
    c_ast_test_expect_failure(arguments, S8("void f(void) { case: }"));
    c_ast_test_expect_failure(arguments, S8("int f(int a,) ;"));
    c_ast_test_expect_failure(arguments, S8("void f(void) { goto ; }"));
    c_ast_test_expect_failure(arguments, S8("struct { int a } s;"));
    return result;
}

// ---- deep nesting ----------------------------------------------------------

// prefix, `open` repeated depth times, middle, `close` repeated depth times,
// suffix.
BUSTER_GLOBAL_LOCAL String8 c_ast_test_nest(Arena* arena, String8 prefix, String8 open, String8 middle, String8 close, u32 depth, String8 suffix)
{
    u64 length = prefix.length + (u64)depth * (open.length + close.length) + middle.length + suffix.length;
    char8* bytes = arena_allocate(arena, char8, length);
    u64 position = 0;
    memcpy(bytes + position, prefix.pointer, prefix.length);
    position += prefix.length;
    for (u32 index = 0; index < depth; index += 1)
    {
        memcpy(bytes + position, open.pointer, open.length);
        position += open.length;
    }
    memcpy(bytes + position, middle.pointer, middle.length);
    position += middle.length;
    for (u32 index = 0; index < depth; index += 1)
    {
        memcpy(bytes + position, close.pointer, close.length);
        position += close.length;
    }
    memcpy(bytes + position, suffix.pointer, suffix.length);
    return (String8){.pointer = bytes, .length = length};
}

// A source nested `depth` deep must build in every layout, validate, and
// reach at least the node and stack depths its construct implies. The C
// stack never grows with the depth: the test would overflow it otherwise.
BUSTER_GLOBAL_LOCAL UnitTestResult c_ast_test_deep_case(UnitTestArguments* arguments, String8 source, u64 minimum_nodes, u64 minimum_frames,
                                                        u64 minimum_operators)
{
    UnitTestResult result = {0};
    TemporalArena temporary = scratch_begin(&arguments->arena, 1);
    CPreprocessResult preprocess = c_ast_test_preprocess(temporary.arena, source, C_PREPROCESS_DIALECT_GNU17);
    if (BUSTER_REQUIRE(arguments, preprocess.error_count == 0))
    {
        for (u32 layout = 0; layout < BUSTER_ARRAY_LENGTH(c_ast_test_layouts); layout += 1)
        {
            CAstResult built = c_ast_build(temporary.arena, preprocess, (CAstOptions){.layout = c_ast_test_layouts[layout]});
            if (!built.complete && built.diagnostic_count)
            {
                string_print_error(S8("c_ast deep case failed: {S8} at line {u32} column {u32}\n"), built.diagnostics[0].message,
                                   built.diagnostics[0].location.line, built.diagnostics[0].location.column);
            }
            if (BUSTER_REQUIRE(arguments, built.complete && built.diagnostic_count == 0))
            {
                if (built.ast.node_count < minimum_nodes)
                {
                    string_print_error(S8("c_ast deep case small: nodes {u64} minimum {u64} frames {u64} operators {u64}\n"), (u64)built.ast.node_count, minimum_nodes,
                                       built.statistics.frame_high_water, built.statistics.operator_high_water);
                }
                BUSTER_TEST(arguments, built.ast.node_count >= minimum_nodes);
                BUSTER_TEST(arguments, built.statistics.node_count == built.ast.node_count);
                BUSTER_TEST(arguments, built.statistics.frame_high_water >= minimum_frames);
                BUSTER_TEST(arguments, built.statistics.operator_high_water >= minimum_operators);
                BUSTER_TEST(arguments, c_ast_validate(&built.ast) == C_AST_NODE_INVALID);
            }
            c_ast_release(&built.ast);
        }
    }
    c_test_scratch_end(temporary);
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult c_ast_test_deep(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    enum
    {
        DEEP = 200000,
        DEEP_BLOCKS = 50000,
        DEEP_STATEMENTS = 100000,
    };
    Arena* arena = arguments->arena;
    // Parentheses are markers on the operator stack, not frames.
    c_ast_test_merge(&result, c_ast_test_deep_case(arguments, c_ast_test_nest(arena, S8("int x = "), S8("("), S8("1"), S8(")"), DEEP, S8(";")), 1, 0, DEEP));
    c_ast_test_merge(&result, c_ast_test_deep_case(arguments, c_ast_test_nest(arena, S8("int x = "), S8("- "), S8("1"), S8(""), DEEP, S8(";")), DEEP, 0, DEEP));
    c_ast_test_merge(&result, c_ast_test_deep_case(arguments, c_ast_test_nest(arena, S8("int x = "), S8("(int)"), S8("1"), S8(""), DEEP, S8(";")), 2 * DEEP, 0, DEEP));
    c_ast_test_merge(&result, c_ast_test_deep_case(arguments, c_ast_test_nest(arena, S8("int x = "), S8("sizeof "), S8("1"), S8(""), DEEP, S8(";")), DEEP, 0, DEEP));
    c_ast_test_merge(&result, c_ast_test_deep_case(arguments, c_ast_test_nest(arena, S8("int x; void f(void) { x"), S8("= x"), S8(""), S8(""), DEEP, S8("; }")), DEEP, 0, DEEP));
    c_ast_test_merge(&result, c_ast_test_deep_case(arguments, c_ast_test_nest(arena, S8("int x; void f(void) { x"), S8("? x : x"), S8(""), S8(""), DEEP, S8("; }")), DEEP, 0, DEEP));
    c_ast_test_merge(&result, c_ast_test_deep_case(arguments, c_ast_test_nest(arena, S8("int x; void f(void) { x"), S8("+ x"), S8(""), S8(""), DEEP, S8("; }")), DEEP, 0, 1));
    c_ast_test_merge(&result, c_ast_test_deep_case(arguments, c_ast_test_nest(arena, S8("int x; void f(void) { x"), S8("[0]"), S8(""), S8(""), DEEP, S8("; }")), DEEP, 0, 0));
    c_ast_test_merge(&result, c_ast_test_deep_case(arguments, c_ast_test_nest(arena, S8("int x; void f(void) { x"), S8(".m"), S8(""), S8(""), DEEP, S8("; }")), DEEP, 0, 0));
    // Blocks, statements and labels are frames.
    c_ast_test_merge(&result, c_ast_test_deep_case(arguments, c_ast_test_nest(arena, S8("void f(void) "), S8("{"), S8(""), S8("}"), DEEP_BLOCKS, S8("")), DEEP_BLOCKS, DEEP_BLOCKS, 0));
    c_ast_test_merge(&result, c_ast_test_deep_case(arguments, c_ast_test_nest(arena, S8("int x; void f(void) { "), S8("if (x) "), S8(";"), S8(""), DEEP_STATEMENTS, S8(" }")), DEEP_STATEMENTS,
                                                  DEEP_STATEMENTS, 0));
    c_ast_test_merge(&result, c_ast_test_deep_case(arguments, c_ast_test_nest(arena, S8("void f(void) { "), S8("l:"), S8(";"), S8(""), DEEP_STATEMENTS, S8(" }")), DEEP_STATEMENTS,
                                                  DEEP_STATEMENTS, 0));
    c_ast_test_merge(&result, c_ast_test_deep_case(arguments, c_ast_test_nest(arena, S8("int x; void f(void) { "), S8("while (x) "), S8(";"), S8(""), DEEP_STATEMENTS, S8(" }")),
                                                  DEEP_STATEMENTS, DEEP_STATEMENTS, 0));
    // Declarators and initializers nest by frames.
    c_ast_test_merge(&result, c_ast_test_deep_case(arguments, c_ast_test_nest(arena, S8("int a"), S8("[1]"), S8(""), S8(""), DEEP_STATEMENTS, S8(";")), DEEP_STATEMENTS, 0, 0));
    c_ast_test_merge(&result, c_ast_test_deep_case(arguments, c_ast_test_nest(arena, S8("int "), S8("("), S8("x"), S8(")"), DEEP_STATEMENTS, S8(";")), 5, DEEP_STATEMENTS, 0));
    c_ast_test_merge(&result, c_ast_test_deep_case(arguments, c_ast_test_nest(arena, S8("int "), S8("*"), S8("x"), S8(""), DEEP, S8(";")), DEEP, 0, 0));
    c_ast_test_merge(&result, c_ast_test_deep_case(arguments, c_ast_test_nest(arena, S8("int a[1] = "), S8("{"), S8("1"), S8("}"), DEEP_STATEMENTS, S8(";")), DEEP_STATEMENTS,
                                                  DEEP_STATEMENTS, 0));
    c_ast_test_merge(&result, c_ast_test_deep_case(arguments, c_ast_test_nest(arena, S8("struct S "), S8("{ struct "), S8("{ int x; }"), S8("; }"), DEEP_BLOCKS, S8(";")), DEEP_BLOCKS,
                                                  DEEP_BLOCKS, 0));
    c_ast_test_merge(&result, c_ast_test_deep_case(arguments, c_ast_test_nest(arena, S8("int x = "), S8("_Generic(1, int: "), S8("1"), S8(")"), DEEP_BLOCKS, S8(";")), DEEP_BLOCKS,
                                                  DEEP_BLOCKS, 0));
    // A call with a very long argument list: one frame, one argument at a time.
    c_ast_test_merge(&result, c_ast_test_deep_case(arguments, c_ast_test_nest(arena, S8("int g(); void f(void) { g("), S8("1, "), S8("1"), S8(""), DEEP, S8("); }")), DEEP, 0, 0));
    c_ast_test_merge(&result, c_ast_test_deep_case(arguments, c_ast_test_nest(arena, S8("int a[] = { "), S8("1, "), S8("1"), S8(""), DEEP, S8(" };")), DEEP, 0, 0));
    return result;
}

// ---- truncation and corruption --------------------------------------------

// A build is complete and valid, or failed with one diagnostic and no tree.
BUSTER_GLOBAL_LOCAL UnitTestResult c_ast_test_check_outcome(UnitTestArguments* arguments, CAstResult const* built)
{
    UnitTestResult result = {0};
    if (built->complete)
    {
        BUSTER_TEST(arguments, built->diagnostic_count == 0);
        BUSTER_TEST(arguments, c_ast_validate(&built->ast) == C_AST_NODE_INVALID);
    }
    else
    {
        BUSTER_TEST(arguments, built->diagnostic_count == 1);
        BUSTER_TEST(arguments, built->ast.node_count == 0);
        BUSTER_TEST(arguments, built->ast.root == C_AST_NODE_INVALID);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL String8 const c_ast_test_programs[] = {
    S8_INITIALIZER("typedef int T; struct S { int a; unsigned b : 3; } s = { .a = 1 }; enum E { A, B = 2 }; T f(int a, char *b) { int x[3] = {1, 2}; "
                   "for (int i = 0; i < 3; i++) { if (a) x[i] = (T)b[i] ? a : -1; else switch (a) { case 1: break; default: ; } } "
                   "return sizeof(T) + x[0]; }"),
    S8_INITIALIZER("int (*(*fp)(int))[2]; void g(int, ...); int h(a, b) int a; { return a; } _Static_assert(1, \"m\"); asm(\"nop\"); "
                   "void k(void) { int x, y; asm volatile (\"\" : \"=r\"(x) : \"r\"(y), [n] \"i\"(1) : \"memory\"); goto L; L: ; }"),
    S8_INITIALIZER("void f(void) { int y = _Generic(1, int: 2, default: 3); int *p = (int[]){1, 2}; ({ y; }); y = y ?: 1; __extension__ y++; "
                   "void *l = &&M; M: ; do y--; while (y); while (y) y--; }"),
    S8_INITIALIZER("__attribute__((noreturn)) void die(void); struct P { int x __attribute__((aligned(8))); } __attribute__((packed)); "
                   "[[nodiscard]] int c(void); int first, __attribute__((unused)) second; enum F : unsigned char { X, Y, }; "
                   "int a[] = { [1] = 2, [3 ... 4] = 5 }; typeof(int) t; _Atomic(int) u; _Alignas(8) int v;"),
    S8_INITIALIZER("int g, __attribute__((x)) h __attribute__((y)) = 1; int (__attribute__((x)) fp)(void); int (__attribute__((x)) (*q))[2]; "
                   "struct Q { int a, __attribute__((x)) b : 2 __attribute__((y)); int (__attribute__((x)) m)[2]; }; "
                   "void k(int (__attribute__((x)) p), int (__attribute__((x)) *)) { sizeof(int (__attribute__((x)) [3])); }"),
};

// Cutting a program anywhere (mid-token included) and deleting or repeating
// any one token must fail cleanly or build a valid tree, never crash or loop.
BUSTER_GLOBAL_LOCAL UnitTestResult c_ast_test_truncations(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    for (u32 program = 0; program < BUSTER_ARRAY_LENGTH(c_ast_test_programs); program += 1)
    {
        String8 text = c_ast_test_programs[program];
        for (u64 cut = 0; cut <= text.length; cut += 1)
        {
            TemporalArena temporary = scratch_begin(&arguments->arena, 1);
            CPreprocessResult preprocess = c_ast_test_preprocess(temporary.arena, string_slice(text, 0, cut), C_PREPROCESS_DIALECT_GNU17);
            if (preprocess.error_count == 0)
            {
                CAstResult built = c_ast_build(temporary.arena, preprocess, (CAstOptions){.refill_batch = (u32)(cut % 3)});
                c_ast_test_merge(&result, c_ast_test_check_outcome(arguments, &built));
                c_ast_release(&built.ast);
            }
            c_test_scratch_end(temporary);
        }
        TemporalArena temporary = scratch_begin(&arguments->arena, 1);
        CPreprocessResult preprocess = c_ast_test_preprocess(temporary.arena, text, C_PREPROCESS_DIALECT_GNU17);
        if (BUSTER_REQUIRE(arguments, preprocess.error_count == 0 && preprocess.token_count > 8))
        {
            CToken* original = preprocess.tokens;
            u64 count = preprocess.token_count;
            CToken* edited = arena_allocate(temporary.arena, CToken, count + 1);
            for (u64 at = 0; at + 1 < count; at += 1)
            {
                for (u32 edit = 0; edit < 2; edit += 1)
                {
                    u64 length = count;
                    memcpy(edited, original, count * sizeof(CToken));
                    if (edit == 0)
                    {
                        memmove(edited + at, edited + at + 1, (length - at - 1) * sizeof(CToken));
                        length -= 1;
                    }
                    else
                    {
                        memmove(edited + at + 1, edited + at, (length - at) * sizeof(CToken));
                        length += 1;
                    }
                    CPreprocessResult changed = preprocess;
                    changed.tokens = edited;
                    changed.token_count = length;
                    CAstResult built = c_ast_build(temporary.arena, changed, (CAstOptions){.refill_batch = (u32)(at % 3)});
                    c_ast_test_merge(&result, c_ast_test_check_outcome(arguments, &built));
                    c_ast_release(&built.ast);
                }
            }
        }
        c_test_scratch_end(temporary);
    }
    return result;
}

// A copy in `arena` that owns no column arenas, so corrupting it leaves the
// built tree intact and releasing the built tree leaves the copy readable.
BUSTER_GLOBAL_LOCAL CAst c_ast_test_copy(Arena* arena, CAst const* ast)
{
    CAst copy = *ast;
    copy.storage = 0;
    u64 count = ast->node_count;
    copy.kinds = arena_allocate(arena, u8, count);
    copy.extents = arena_allocate(arena, u32, count);
    copy.tokens = arena_allocate(arena, u32, count);
    copy.data = arena_allocate(arena, u32, count);
    memcpy(copy.kinds, ast->kinds, count);
    memcpy(copy.extents, ast->extents, count * sizeof(u32));
    memcpy(copy.tokens, ast->tokens, count * sizeof(u32));
    memcpy(copy.data, ast->data, count * sizeof(u32));
    if (ast->children_count)
    {
        copy.children = arena_allocate(arena, u32, ast->children_count);
        memcpy(copy.children, ast->children, (u64)ast->children_count * sizeof(u32));
    }
    if (ast->slices)
    {
        copy.slices = arena_allocate(arena, u32, count);
        memcpy(copy.slices, ast->slices, count * sizeof(u32));
    }
    return copy;
}

// c_ast_validate names the first broken node of a corrupted tree.
BUSTER_GLOBAL_LOCAL UnitTestResult c_ast_test_validator(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    TemporalArena temporary = scratch_begin(&arguments->arena, 1);
    CPreprocessResult preprocess = c_ast_test_preprocess(temporary.arena, S8("int x = 1 + 2; int a[2] = {1, 2}; void f(void) { x = a[1]; }"), C_PREPROCESS_DIALECT_GNU17);
    for (u32 layout = 0; layout < BUSTER_ARRAY_LENGTH(c_ast_test_layouts); layout += 1)
    {
        CAstResult built = c_ast_build(temporary.arena, preprocess, (CAstOptions){.layout = c_ast_test_layouts[layout]});
        if (BUSTER_REQUIRE(arguments, built.complete))
        {
            CAst const* ast = &built.ast;
            BUSTER_TEST(arguments, c_ast_validate(ast) == C_AST_NODE_INVALID);
            u32 add = c_ast_test_find_kind(ast, C_AST_ADD);
            u32 list = c_ast_test_find_kind(ast, C_AST_INITIALIZER_LIST);
            u32 init = c_ast_test_find_kind(ast, C_AST_INIT_DECLARATOR);
            if (BUSTER_REQUIRE(arguments, add != C_AST_NODE_INVALID && list != C_AST_NODE_INVALID && init != C_AST_NODE_INVALID))
            {
                CAst broken = c_ast_test_copy(temporary.arena, ast);
                broken.extents[add] = 2;
                BUSTER_TEST(arguments, c_ast_validate(&broken) != C_AST_NODE_INVALID);
                broken = c_ast_test_copy(temporary.arena, ast);
                broken.extents[add] = 0;
                BUSTER_TEST(arguments, c_ast_validate(&broken) != C_AST_NODE_INVALID);
                broken = c_ast_test_copy(temporary.arena, ast);
                broken.extents[add] = add + 2;
                BUSTER_TEST(arguments, c_ast_validate(&broken) != C_AST_NODE_INVALID);
                broken = c_ast_test_copy(temporary.arena, ast);
                broken.kinds[add] = C_AST_KIND_COUNT;
                BUSTER_TEST(arguments, c_ast_validate(&broken) != C_AST_NODE_INVALID);
                broken = c_ast_test_copy(temporary.arena, ast);
                broken.kinds[add] = C_AST_NUMBER;
                BUSTER_TEST(arguments, c_ast_validate(&broken) != C_AST_NODE_INVALID);
                broken = c_ast_test_copy(temporary.arena, ast);
                broken.data[init] ^= 4;
                BUSTER_TEST(arguments, c_ast_validate(&broken) != C_AST_NODE_INVALID);
                broken = c_ast_test_copy(temporary.arena, ast);
                broken.root = ast->root - 1;
                BUSTER_TEST(arguments, c_ast_validate(&broken) != C_AST_NODE_INVALID);
                broken = c_ast_test_copy(temporary.arena, ast);
                broken.extents[ast->root] -= 1;
                BUSTER_TEST(arguments, c_ast_validate(&broken) != C_AST_NODE_INVALID);
                broken = c_ast_test_copy(temporary.arena, ast);
                if (ast->layout == C_AST_LAYOUT_IMPLICIT || ast->layout == C_AST_LAYOUT_EXPLICIT)
                {
                    broken.data[list] += 1;
                    BUSTER_TEST(arguments, c_ast_validate(&broken) != C_AST_NODE_INVALID);
                }
                if (ast->layout == C_AST_LAYOUT_HYBRID)
                {
                    broken.children[broken.data[list] + 1] += 1;
                    BUSTER_TEST(arguments, c_ast_validate(&broken) != C_AST_NODE_INVALID);
                }
                if (ast->layout == C_AST_LAYOUT_EXPLICIT)
                {
                    broken = c_ast_test_copy(temporary.arena, ast);
                    broken.slices[add] = C_AST_NODE_INVALID;
                    BUSTER_TEST(arguments, c_ast_validate(&broken) != C_AST_NODE_INVALID);
                }
            }
        }
        c_ast_release(&built.ast);
    }
    c_test_scratch_end(temporary);
    return result;
}

BUSTER_GLOBAL_LOCAL u32 c_ast_test_find_last_kind(CAst const* ast, CAstKind kind)
{
    u32 result = C_AST_NODE_INVALID;
    for (u32 node = ast->node_count; node > 0 && result == C_AST_NODE_INVALID; node -= 1)
    {
        if (ast->kinds[node - 1] == kind)
        {
            result = node - 1;
        }
    }
    return result;
}

// Builds `source` in every layout and holds its last `kind` node to its data
// word (the presence bits; zero for a FIXED kind), its children's kinds in
// order, and source order (the children's anchor tokens increase). Any one
// presence bit flipped must break the contract.
BUSTER_GLOBAL_LOCAL UnitTestResult c_ast_test_check_slots(UnitTestArguments* arguments, String8 source, CAstKind kind, u32 data, CAstKind const* children,
                                                          u32 child_count)
{
    UnitTestResult result = {0};
    TemporalArena temporary = scratch_begin(&arguments->arena, 1);
    CPreprocessResult preprocess = c_ast_test_preprocess(temporary.arena, source, C_PREPROCESS_DIALECT_GNU17);
    for (u32 layout = 0; layout < BUSTER_ARRAY_LENGTH(c_ast_test_layouts); layout += 1)
    {
        CAstResult built = c_ast_build(temporary.arena, preprocess, (CAstOptions){.layout = c_ast_test_layouts[layout]});
        if (BUSTER_REQUIRE(arguments, built.complete))
        {
            CAst const* ast = &built.ast;
            u32 node = c_ast_test_find_last_kind(ast, kind);
            if (BUSTER_REQUIRE(arguments, node != C_AST_NODE_INVALID && c_ast_child_count(ast, node) == child_count))
            {
                BUSTER_TEST_RAW(arguments, ast->data[node] == data, source);
                u32 previous_token = 0;
                for (u32 index = 0; index < child_count; index += 1)
                {
                    u32 child = c_ast_child_at(ast, node, index);
                    BUSTER_TEST_RAW(arguments, ast->kinds[child] == children[index], source);
                    BUSTER_TEST_RAW(arguments, index == 0 || ast->tokens[child] > previous_token, source);
                    previous_token = ast->tokens[child];
                }
                if (kind == C_AST_DECLARATOR_ATTRIBUTED)
                {
                    // The anchor is the attribute list's first token.
                    BUSTER_TEST_RAW(arguments, ast->tokens[node] == ast->tokens[c_ast_child_at(ast, node, 0)], source);
                }
                if (c_ast_kind_contract(kind) == C_AST_CONTRACT_PRESENCE)
                {
                    for (u32 bit = 0; bit < 4; bit += 1)
                    {
                        CAst broken = c_ast_test_copy(temporary.arena, ast);
                        broken.data[node] ^= 1u << bit;
                        BUSTER_TEST_RAW(arguments, c_ast_validate(&broken) != C_AST_NODE_INVALID, source);
                    }
                }
            }
        }
        c_ast_release(&built.ast);
    }
    c_test_scratch_end(temporary);
    return result;
}

// The attribute slots of INIT_DECLARATOR (bit 3 leading, bit 1 trailing),
// MEMBER_DECLARATOR (bit 3 leading, bit 2 trailing) and the FIXED
// DECLARATOR_ATTRIBUTED, as c_ast.h states them.
// A vendor attribute's argument clause is a balanced token sequence (C23
// 6.7.13.2): `vendor::tag({gnu::error("m")})` is stepped over to its closing
// parenthesis and appends no argument node, while a `gnu::` attribute's
// arguments are still expressions. An unbalanced clause fails the build.
BUSTER_GLOBAL_LOCAL UnitTestResult c_ast_test_vendor_attribute_arguments(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    TemporalArena temporary = scratch_begin(&arguments->arena, 1);
    CPreprocessResult preprocess = c_ast_test_preprocess(
        temporary.arena, S8("[[gnu::unused, vendor::tag({gnu::error(\"m\")}), gnu::aligned(8)]] void f(void);\nvoid t(void) { f(); }\n"),
        C_PREPROCESS_DIALECT_GNU23);
    CAstResult built = c_ast_build(temporary.arena, preprocess, (CAstOptions){0});
    if (BUSTER_REQUIRE(arguments, built.complete))
    {
        u32 vendor = C_AST_NODE_INVALID;
        u32 aligned = C_AST_NODE_INVALID;
        for (u32 node = 0; node < built.ast.node_count; node += 1)
        {
            if (built.ast.kinds[node] == C_AST_ATTRIBUTE_SCOPED)
            {
                String8 name = c_token_spelling(preprocess.spelling_base, preprocess.tokens[built.ast.tokens[node]]);
                vendor = string_equal(name, S8("tag")) ? node : vendor;
                aligned = string_equal(name, S8("aligned")) ? node : aligned;
            }
        }
        // The namespace alone; `aligned` keeps its argument expression.
        BUSTER_TEST(arguments, vendor != C_AST_NODE_INVALID && c_ast_child_count(&built.ast, vendor) == 1);
        BUSTER_TEST(arguments, aligned != C_AST_NODE_INVALID && c_ast_child_count(&built.ast, aligned) == 2);
        BUSTER_TEST(arguments, c_ast_validate(&built.ast) == C_AST_NODE_INVALID);
    }
    c_ast_release(&built.ast);
    CPreprocessResult unbalanced = c_ast_test_preprocess(temporary.arena, S8("[[vendor::tag({)]] void f(void);\n"), C_PREPROCESS_DIALECT_GNU23);
    CAstResult refused = c_ast_build(temporary.arena, unbalanced, (CAstOptions){0});
    BUSTER_TEST(arguments, !refused.complete && refused.diagnostic_count != 0);
    c_ast_release(&refused.ast);
    c_test_scratch_end(temporary);
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult c_ast_test_attribute_slots(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    CAstKind const leading_trailing_initializer[] = {C_AST_ATTRIBUTE_LIST, C_AST_DECLARATOR_NAME, C_AST_ATTRIBUTE_LIST, C_AST_NUMBER};
    CAstKind const leading_asm_trailing[] = {C_AST_ATTRIBUTE_LIST, C_AST_DECLARATOR_NAME, C_AST_ASM_LABEL, C_AST_ATTRIBUTE_LIST};
    CAstKind const leading_only[] = {C_AST_ATTRIBUTE_LIST, C_AST_DECLARATOR_NAME};
    CAstKind const trailing_only[] = {C_AST_DECLARATOR_NAME, C_AST_ATTRIBUTE_LIST};
    CAstKind const member_all[] = {C_AST_ATTRIBUTE_LIST, C_AST_DECLARATOR_NAME, C_AST_NUMBER, C_AST_ATTRIBUTE_LIST};
    CAstKind const member_unnamed[] = {C_AST_ATTRIBUTE_LIST, C_AST_NUMBER, C_AST_ATTRIBUTE_LIST};
    CAstKind const attributed_name[] = {C_AST_ATTRIBUTE_LIST, C_AST_DECLARATOR_NAME};
    CAstKind const attributed_array[] = {C_AST_ATTRIBUTE_LIST, C_AST_DECLARATOR_ARRAY};
    c_ast_test_merge(&result, c_ast_test_check_slots(arguments, S8("int a, __attribute__((x)) b __attribute__((y)) = 1;"), C_AST_INIT_DECLARATOR, 2u | 4u | 8u,
                                                     leading_trailing_initializer, BUSTER_ARRAY_LENGTH(leading_trailing_initializer)));
    c_ast_test_merge(&result, c_ast_test_check_slots(arguments, S8("int a, __attribute__((x)) b __asm__(\"q\") __attribute__((y));"), C_AST_INIT_DECLARATOR,
                                                     1u | 2u | 8u, leading_asm_trailing, BUSTER_ARRAY_LENGTH(leading_asm_trailing)));
    c_ast_test_merge(&result, c_ast_test_check_slots(arguments, S8("int a, __attribute__((x)) b;"), C_AST_INIT_DECLARATOR, 8u, leading_only,
                                                     BUSTER_ARRAY_LENGTH(leading_only)));
    c_ast_test_merge(&result, c_ast_test_check_slots(arguments, S8("int a __attribute__((y));"), C_AST_INIT_DECLARATOR, 2u, trailing_only,
                                                     BUSTER_ARRAY_LENGTH(trailing_only)));
    c_ast_test_merge(&result, c_ast_test_check_slots(arguments, S8("struct S { int a, __attribute__((x)) b : 3 __attribute__((y)); };"), C_AST_MEMBER_DECLARATOR,
                                                     1u | 2u | 4u | 8u, member_all, BUSTER_ARRAY_LENGTH(member_all)));
    c_ast_test_merge(&result, c_ast_test_check_slots(arguments, S8("struct S { int a, __attribute__((x)) b; };"), C_AST_MEMBER_DECLARATOR, 1u | 8u,
                                                     leading_only, BUSTER_ARRAY_LENGTH(leading_only)));
    c_ast_test_merge(&result, c_ast_test_check_slots(arguments, S8("struct S { int a, __attribute__((x)) : 3 __attribute__((y)); };"), C_AST_MEMBER_DECLARATOR,
                                                     2u | 4u | 8u, member_unnamed, BUSTER_ARRAY_LENGTH(member_unnamed)));
    c_ast_test_merge(&result, c_ast_test_check_slots(arguments, S8("int (__attribute__((x)) p);"), C_AST_DECLARATOR_ATTRIBUTED, 0u, attributed_name,
                                                     BUSTER_ARRAY_LENGTH(attributed_name)));
    c_ast_test_merge(&result, c_ast_test_check_slots(arguments, S8("int (__attribute__((x)) p[3]);"), C_AST_DECLARATOR_ATTRIBUTED, 0u, attributed_array,
                                                     BUSTER_ARRAY_LENGTH(attributed_array)));
    return result;
}

// ---- accessors, traversal, names and statistics ---------------------------

BUSTER_GLOBAL_LOCAL UnitTestResult c_ast_test_navigation(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    TemporalArena temporary = scratch_begin(&arguments->arena, 1);
    CPreprocessResult preprocess = c_ast_test_preprocess(temporary.arena, S8("int a[2] = {1, 2}; void f(void) { a[0] = 3; if (a[1]) return; }"),
                                                          C_PREPROCESS_DIALECT_GNU17);
    for (u32 layout = 0; layout < BUSTER_ARRAY_LENGTH(c_ast_test_layouts); layout += 1)
    {
        CAstResult built = c_ast_build(temporary.arena, preprocess, (CAstOptions){.layout = c_ast_test_layouts[layout]});
        if (BUSTER_REQUIRE(arguments, built.complete))
        {
            CAst const* ast = &built.ast;
            u32 list = c_ast_test_find_kind(ast, C_AST_INITIALIZER_LIST);
            if (BUSTER_REQUIRE(arguments, list != C_AST_NODE_INVALID))
            {
                BUSTER_TEST(arguments, c_ast_list_count(ast, list) == 2);
                BUSTER_TEST(arguments, c_ast_child_count(ast, list) == 2);
                BUSTER_TEST(arguments, c_ast_child_at(ast, list, 0) == list - 2);
                BUSTER_TEST(arguments, c_ast_child_at(ast, list, 1) == list - 1);
                BUSTER_TEST(arguments, c_ast_child_at(ast, list, 2) == C_AST_NODE_INVALID);
                u32 roots[2] = {C_AST_NODE_INVALID, C_AST_NODE_INVALID};
                BUSTER_TEST(arguments, c_ast_children(ast, list, roots, 1) == 2);
                BUSTER_TEST(arguments, roots[0] == list - 2 && roots[1] == C_AST_NODE_INVALID);
                BUSTER_TEST(arguments, c_ast_subtree_begin(ast, list) == list - 2);
            }
            BUSTER_TEST(arguments, c_ast_child_count(ast, ast->root) == c_ast_list_count(ast, ast->root));
            // The traversal visits each node once and exits each interior node
            // after its last descendant.
            u32* open = arena_allocate(temporary.arena, u32, ast->node_count);
            u32 depth = 0;
            u32 enters = 0;
            u32 exits = 0;
            bool nested = true;
            CAstWalk walk = c_ast_walk_begin(temporary.arena, ast, ast->root);
            u32 node = 0;
            CAstWalkEvent event = c_ast_walk_next(&walk, &node);
            while (event != C_AST_WALK_DONE)
            {
                if (event == C_AST_WALK_ENTER)
                {
                    enters += 1;
                    if (ast->extents[node] > 1)
                    {
                        open[depth] = node;
                        depth += 1;
                    }
                }
                else
                {
                    exits += 1;
                    nested = nested && depth > 0 && open[depth - 1] == node;
                    depth -= nested ? 1 : 0;
                }
                event = c_ast_walk_next(&walk, &node);
            }
            u32 interior = 0;
            for (u32 index = 0; index < ast->node_count; index += 1)
            {
                interior += ast->extents[index] > 1;
            }
            BUSTER_TEST(arguments, nested && depth == 0);
            BUSTER_TEST(arguments, enters == ast->node_count);
            BUSTER_TEST(arguments, exits == interior);
            BUSTER_TEST(arguments, walk.steps >= ast->node_count && walk.steps <= 3 * (u64)ast->node_count);
        }
        c_ast_release(&built.ast);
    }
    c_test_scratch_end(temporary);
    return result;
}

BUSTER_GLOBAL_LOCAL bool c_ast_test_lowercase_equal(String8 lower, String8 upper)
{
    bool equal = lower.length == upper.length;
    for (u64 index = 0; index < lower.length && equal; index += 1)
    {
        char8 character = upper.pointer[index];
        equal = lower.pointer[index] == (character >= 'A' && character <= 'Z' ? (char8)(character - 'A' + 'a') : character);
    }
    return equal;
}

BUSTER_GLOBAL_LOCAL UnitTestResult c_ast_test_tables(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
#define C_AST_TEST_KIND_NAME(name, contract, a, b)                                                    \
    BUSTER_TEST(arguments, c_ast_test_lowercase_equal(c_ast_kind_name(C_AST_##name), S8(#name)));     \
    BUSTER_TEST(arguments, c_ast_kind_contract(C_AST_##name) == C_AST_CONTRACT_##contract);
    C_AST_KIND_LIST(C_AST_TEST_KIND_NAME)
#undef C_AST_TEST_KIND_NAME
#define C_AST_TEST_WORD_NAME(name) BUSTER_TEST(arguments, c_ast_word_name(C_AST_WORD_##name).length > 0);
    C_AST_WORD_LIST(C_AST_TEST_WORD_NAME)
#undef C_AST_TEST_WORD_NAME
    BUSTER_STRING_TEST(arguments, c_ast_word_name(C_AST_WORD_CONST), S8("const"));
    BUSTER_STRING_TEST(arguments, c_ast_word_name(C_AST_WORD_THREAD_LOCAL), S8("_Thread_local"));
    BUSTER_STRING_TEST(arguments, c_ast_kind_name(C_AST_KIND_COUNT), S8("invalid"));
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult c_ast_test_options_and_statistics(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    TemporalArena temporary = scratch_begin(&arguments->arena, 1);
    CPreprocessResult preprocess = c_ast_test_preprocess(temporary.arena, c_ast_test_programs[0], C_PREPROCESS_DIALECT_GNU17);
    CAstResult implicit_result = c_ast_build(temporary.arena, preprocess, (CAstOptions){0});
    if (BUSTER_REQUIRE(arguments, implicit_result.complete))
    {
        CAstStatistics statistics = implicit_result.statistics;
        BUSTER_TEST(arguments, statistics.node_count == implicit_result.ast.node_count);
        BUSTER_TEST(arguments, statistics.retained_bytes == (u64)implicit_result.ast.node_count * 13 +
                                                           (u64)implicit_result.ast.parenthesis_count * sizeof(CAstParenthesis));
        // The columns are written where the tree keeps them: nothing is copied.
        BUSTER_TEST(arguments, statistics.column_copy_bytes == 0);
        BUSTER_TEST(arguments, statistics.transient_high_water > 0);
        BUSTER_TEST(arguments, statistics.finalize_child_entries == 0);
        BUSTER_TEST(arguments, statistics.tokens_consumed + 1 == preprocess.token_count);
        BUSTER_TEST(arguments, statistics.refills == 0);
        BUSTER_TEST(arguments, statistics.frames_pushed > 0 && statistics.frame_high_water > 1 && statistics.bindings_published > 0);
        // Every layout adds only its slices to the retained bytes.
        CAstResult hybrid = c_ast_build(temporary.arena, preprocess, (CAstOptions){.layout = C_AST_LAYOUT_HYBRID});
        CAstResult explicit_result = c_ast_build(temporary.arena, preprocess, (CAstOptions){.layout = C_AST_LAYOUT_EXPLICIT});
        BUSTER_TEST(arguments, hybrid.complete && explicit_result.complete);
        BUSTER_TEST(arguments, hybrid.statistics.finalize_child_entries > 0 && hybrid.ast.children_count == hybrid.statistics.finalize_child_entries);
        BUSTER_TEST(arguments, explicit_result.statistics.finalize_child_entries >= hybrid.statistics.finalize_child_entries);
        BUSTER_TEST(arguments, hybrid.statistics.retained_bytes == statistics.retained_bytes + hybrid.statistics.finalize_child_entries * 4);
        BUSTER_TEST(arguments, explicit_result.statistics.retained_bytes ==
                                   statistics.retained_bytes + explicit_result.statistics.finalize_child_entries * 4 + (u64)explicit_result.ast.node_count * 4);
        c_ast_release(&explicit_result.ast);
        c_ast_release(&hybrid.ast);
        // The ring window refills in batches.
        CAstResult batched = c_ast_build(temporary.arena, preprocess, (CAstOptions){.refill_batch = 5});
        BUSTER_TEST(arguments, batched.complete && batched.statistics.refills * 5 >= preprocess.token_count);
        BUSTER_TEST(arguments, c_ast_test_same_tree(&implicit_result.ast, &batched.ast, false));
        c_ast_release(&batched.ast);
        // A very large batch is the whole stream in one refill.
        CAstResult whole = c_ast_build(temporary.arena, preprocess, (CAstOptions){.refill_batch = 1u << 20});
        BUSTER_TEST(arguments, whole.complete && whole.statistics.refills == 1);
        c_ast_release(&whole.ast);
        // A supplied phase arena returns to its entry position.
        Arena* phase = arena_create((ArenaCreation){.reserved_size = BUSTER_GB(1)});
        if (BUSTER_REQUIRE(arguments, phase != 0))
        {
            u64 position = phase->position;
            CAstResult shared = c_ast_build(temporary.arena, preprocess, (CAstOptions){.phase_arena = phase});
            BUSTER_TEST(arguments, shared.complete && phase->position == position && shared.statistics.transient_high_water > 0);
            BUSTER_TEST(arguments, c_ast_test_same_tree(&implicit_result.ast, &shared.ast, false));
            // The columns are in neither the phase arena nor the caller's.
            BUSTER_TEST(arguments, !arena_range_contains(phase, arena_minimum_position, phase->reserved_size, shared.ast.kinds));
            BUSTER_TEST(arguments, !arena_range_contains(temporary.arena, arena_minimum_position, temporary.arena->reserved_size, shared.ast.extents));
            c_ast_release(&shared.ast);
            CAstResult failed = c_ast_build(temporary.arena, c_ast_test_preprocess(temporary.arena, S8("int x"), C_PREPROCESS_DIALECT_GNU17),
                                            (CAstOptions){.phase_arena = phase});
            BUSTER_TEST(arguments, !failed.complete && failed.diagnostic_count == 1 && phase->position == position);
            arena_destroy(phase, 1);
        }
    }
    c_ast_release(&implicit_result.ast);
    c_test_scratch_end(temporary);
    return result;
}

// Exact group roots and provenance are specified independently of the
// builder's lookup: call argument delimiters and statement condition
// delimiters are owned by their syntax nodes, not expression groups.
BUSTER_GLOBAL_LOCAL UnitTestResult c_ast_test_parentheses(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    BUSTER_TEST(arguments, c_test_retained_parenthesis_navigation());
    TemporalArena temporary = scratch_begin(&arguments->arena, 1);
    CPreprocessResult preprocess = c_ast_test_preprocess(temporary.arena,
        S8("void f(void) { (a + (b * c)); ((d)); f((e)); (f)(g); }"), C_PREPROCESS_DIALECT_GNU17);
    CAstParenthesis const expected[] = {{11, 9, 13}, {12, 6, 14}, {14, 17, 19}, {14, 16, 20}, {17, 24, 26}, {20, 29, 31}, {24, 5, 36, C_AST_DELIMITER_BLOCK}};
    for (u32 batch = 0; batch < BUSTER_ARRAY_LENGTH(c_ast_test_batches); batch += 1)
    {
        for (u32 layout = 0; layout < BUSTER_ARRAY_LENGTH(c_ast_test_layouts); layout += 1)
        {
            CAstResult built = c_ast_build(temporary.arena, preprocess,
                (CAstOptions){.layout = c_ast_test_layouts[layout], .refill_batch = c_ast_test_batches[batch],
                              .column_capacity = c_ast_test_capacities[batch], .parenthesis_capacity = 1});
            if (BUSTER_REQUIRE(arguments, built.complete && built.ast.parenthesis_count == BUSTER_ARRAY_LENGTH(expected)))
            {
                BUSTER_TEST(arguments, c_ast_validate(&built.ast) == C_AST_NODE_INVALID);
                BUSTER_TEST(arguments, built.statistics.parenthesis_copy_bytes == 7 * sizeof(CAstParenthesis));
                BUSTER_TEST(arguments, built.statistics.parenthesis_committed_bytes == 8 * sizeof(CAstParenthesis));
                for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(expected); index += 1)
                {
                    CAstParenthesis actual = built.ast.parentheses[index];
                    BUSTER_TEST(arguments, actual.node == expected[index].node && actual.open == expected[index].open &&
                                               actual.close == expected[index].close && actual.flags == expected[index].flags);
                }
                CAstParentheses pair = c_ast_parentheses(&built.ast, 14);
                BUSTER_TEST(arguments, pair.first == 2 && pair.count == 2);
                BUSTER_TEST(arguments, c_ast_parenthesis_at(&built.ast, 14, 1) == built.ast.parentheses + 2);
                BUSTER_TEST(arguments, c_ast_parenthesis_at(&built.ast, 14, 2) == built.ast.parentheses + 3);
                BUSTER_TEST(arguments, !c_ast_parenthesis_at(&built.ast, 14, 0) && !c_ast_parenthesis_at(&built.ast, 14, 3));
                BUSTER_TEST(arguments, c_ast_parentheses(&built.ast, 21).count == 0);
                BUSTER_TEST(arguments, c_ast_parentheses(&built.ast, C_AST_NODE_INVALID).count == 0);
                CAstParenthesis saved = built.ast.parentheses[0];
                built.ast.parentheses[0].node = C_AST_NODE_INVALID;
                BUSTER_TEST(arguments, c_ast_validate(&built.ast) == built.ast.root);
                built.ast.parentheses[0] = saved;
                CAst copy = built.ast;
                u64 live = arena_test_live_reserved_bytes();
                c_ast_release(&built.ast);
                BUSTER_TEST(arguments, !built.ast.parentheses && !built.ast.parenthesis_count && arena_test_live_reserved_bytes() < live);
                c_ast_release(&copy);
            }
            c_ast_release(&built.ast);
        }
    }
    u32 depth = 8192;
    CPreprocessResult deep = c_ast_test_preprocess(temporary.arena,
        c_ast_test_nest(temporary.arena, S8("void f(void) { "), S8("("), S8("a"), S8(")"), depth, S8("; }")),
        C_PREPROCESS_DIALECT_GNU17);
    CAstResult nested = c_ast_build(temporary.arena, deep, (CAstOptions){.parenthesis_capacity = 3, .column_capacity = 3, .refill_batch = 3});
    if (BUSTER_REQUIRE(arguments, nested.complete && nested.ast.parenthesis_count == depth + 1))
    {
        BUSTER_TEST(arguments, c_ast_validate(&nested.ast) == C_AST_NODE_INVALID);
        for (u32 level = 0; level < depth; level += 1)
        {
            CAstParenthesis row = nested.ast.parentheses[level];
            BUSTER_TEST(arguments, row.node == 8 && row.open == 5 + depth - level && row.close == 7 + depth + level);
        }
    }
    c_ast_release(&nested.ast);
    CPreprocessResult skew = c_ast_test_preprocess(temporary.arena,
        c_ast_test_nest(temporary.arena, S8("void f(void) { "), S8("(a+"), S8("a"), S8(")"), depth, S8("; }")),
        C_PREPROCESS_DIALECT_GNU17);
    CAstResult chain = c_ast_build(temporary.arena, skew, (CAstOptions){.parenthesis_capacity = 7, .column_capacity = 7, .refill_batch = 1});
    if (BUSTER_REQUIRE(arguments, chain.complete && chain.ast.parenthesis_count == depth + 1))
    {
        BUSTER_TEST(arguments, c_ast_validate(&chain.ast) == C_AST_NODE_INVALID);
        for (u32 level = 0; level < depth; level += 1)
        {
            CAstParenthesis row = chain.ast.parentheses[level];
            BUSTER_TEST(arguments, row.node == 9 + depth + level && row.open == 6 + 3 * (depth - level - 1) && row.close == 7 + 3 * depth + level);
        }
    }
    c_ast_release(&chain.ast);
    CPreprocessResult context = c_ast_test_preprocess(temporary.arena,
        S8("void f(void) { __extension__ (a); sizeof (a); ((a)); }"), C_PREPROCESS_DIALECT_GNU17);
    CAstResult contexts = c_ast_build(temporary.arena, context, (CAstOptions){.parenthesis_capacity = 1});
    if (BUSTER_REQUIRE(arguments, contexts.complete && contexts.ast.parenthesis_count == 5))
    {
        BUSTER_TEST(arguments, contexts.ast.parentheses[0].flags == C_AST_PARENTHESIS_AFTER_IDENTIFIER);
        BUSTER_TEST(arguments, contexts.ast.parentheses[1].flags == C_AST_PARENTHESIS_AFTER_IDENTIFIER);
        BUSTER_TEST(arguments, !contexts.ast.parentheses[2].flags && !contexts.ast.parentheses[3].flags);
    }
    c_ast_release(&contexts.ast);
    CPreprocessResult failed = c_ast_test_preprocess(temporary.arena, S8("void f(void) { ((a)); ((b)"), C_PREPROCESS_DIALECT_GNU17);
    u64 live = arena_test_live_reserved_bytes();
    CAstResult rollback = c_ast_build(temporary.arena, failed, (CAstOptions){.parenthesis_capacity = 1});
    BUSTER_TEST(arguments, !rollback.complete && !rollback.ast.parentheses && !rollback.ast.parenthesis_count && !rollback.ast.node_count);
    BUSTER_TEST(arguments, rollback.diagnostic_count == 1 && arena_test_live_reserved_bytes() == live);
    c_ast_release(&rollback.ast);
    for (u32 ordinal = 6; ordinal <= 9; ordinal += 1)
    {
        live = arena_test_live_reserved_bytes();
        c_test_fail_frontend_reservation(C_FRONTEND_RESERVATION_ANALYSIS, ordinal);
        CAstResult refused = c_ast_build(temporary.arena, preprocess, (CAstOptions){.parenthesis_capacity = 1});
        bool pending = c_test_frontend_reservation_pending();
        c_test_fail_frontend_reservation(C_FRONTEND_RESERVATION_ANALYSIS, 0);
        BUSTER_TEST(arguments, !pending && !refused.complete && !refused.ast.storage && !refused.ast.parenthesis_count);
        BUSTER_TEST(arguments, refused.diagnostic_count == 1 && arena_test_live_reserved_bytes() == live);
        c_ast_release(&refused.ast);
    }
    c_test_scratch_end(temporary);
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult c_ast_test_owned_delimiters(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    TemporalArena temporary = scratch_begin(&arguments->arena, 1);
    CPreprocessResult preprocess = c_ast_test_preprocess(temporary.arena,
        S8("void f(void) { if ((a)) ; while(b) ; do ; while(c); for(;;) ; switch(d); }"), C_PREPROCESS_DIALECT_GNU17);
    CAstParenthesis const expected[] = {
        {8, 8, 10, 0}, {8, 7, 11, C_AST_DELIMITER_CONDITION}, {11, 14, 16, C_AST_DELIMITER_CONDITION},
        {15, 21, 23, C_AST_DELIMITER_CONDITION}, {18, 26, 29, C_AST_DELIMITER_FOR_HEADER},
        {19, 32, 34, C_AST_DELIMITER_CONDITION}, {22, 5, 36, C_AST_DELIMITER_BLOCK},
    };
    for (u32 batch = 0; batch < BUSTER_ARRAY_LENGTH(c_ast_test_batches); batch += 1)
    {
        for (u32 layout = 0; layout < BUSTER_ARRAY_LENGTH(c_ast_test_layouts); layout += 1)
        {
            CAstResult built = c_ast_build(temporary.arena, preprocess,
                (CAstOptions){.layout = c_ast_test_layouts[layout], .refill_batch = c_ast_test_batches[batch],
                              .column_capacity = c_ast_test_capacities[batch], .parenthesis_capacity = 1});
            if (BUSTER_REQUIRE(arguments, built.complete && built.ast.parenthesis_count == BUSTER_ARRAY_LENGTH(expected)))
            {
                BUSTER_TEST(arguments, c_ast_validate(&built.ast) == C_AST_NODE_INVALID);
                for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(expected); index += 1)
                {
                    CAstParenthesis actual = built.ast.parentheses[index];
                    BUSTER_TEST(arguments, actual.node == expected[index].node && actual.open == expected[index].open &&
                                               actual.close == expected[index].close && actual.flags == expected[index].flags);
                }
                BUSTER_TEST(arguments, c_ast_parentheses(&built.ast, 8).count == 1 && !c_ast_parenthesis_at(&built.ast, 8, 2));
                CAstParenthesis const* condition = c_ast_owned_delimiter(&built.ast, 8, C_AST_DELIMITER_CONDITION);
                BUSTER_TEST(arguments, condition && condition->open == 7 && condition->close == 11);
                BUSTER_TEST(arguments, !c_ast_owned_delimiter(&built.ast, 8, C_AST_DELIMITER_FOR_HEADER));
                BUSTER_TEST(arguments, c_ast_parentheses(&built.ast, 18).count == 0 && c_ast_parentheses(&built.ast, 22).count == 0);
            }
            c_ast_release(&built.ast);
        }
    }
    CPreprocessResult failed = c_ast_test_preprocess(temporary.arena, S8("void f(void) { if(a)"), C_PREPROCESS_DIALECT_GNU17);
    u64 live = arena_test_live_reserved_bytes();
    CAstResult rollback = c_ast_build(temporary.arena, failed, (CAstOptions){.parenthesis_capacity = 1});
    BUSTER_TEST(arguments, !rollback.complete && !rollback.ast.node_count && !rollback.ast.parenthesis_count && rollback.diagnostic_count == 1);
    BUSTER_TEST(arguments, arena_test_live_reserved_bytes() == live);
    c_ast_release(&rollback.ast);
    c_test_scratch_end(temporary);
    return result;
}

// `void f(void) { int x; ... }` whose tree has exactly `nodes` nodes, given the
// `base` nodes of the empty body: each `x = 1;` adds four (EXPRESSION_STATEMENT,
// ASSIGN, IDENTIFIER, NUMBER) and each `;` one (NULL_STATEMENT).
BUSTER_GLOBAL_LOCAL String8 c_ast_test_column_source(Arena* arena, u32 base, u32 nodes)
{
    String8 head = S8("void f(void) { int x; ");
    String8 statement = S8("x = 1; ");
    u32 statements = (nodes - base) / 4;
    u32 nulls = (nodes - base) % 4;
    u64 length = head.length + (u64)statements * statement.length + nulls + 1;
    char8* text = arena_allocate(arena, char8, length);
    u64 at = 0;
    memcpy(text, head.pointer, head.length);
    at += head.length;
    for (u32 index = 0; index < statements; index += 1)
    {
        memcpy(text + at, statement.pointer, statement.length);
        at += statement.length;
    }
    for (u32 index = 0; index < nulls; index += 1)
    {
        text[at] = ';';
        at += 1;
    }
    text[at] = '}';
    return (String8){.pointer = text, .length = length};
}

// The bytes c_ast_build copies when its columns start at `capacity` nodes and
// must hold `nodes`: every full reservation moves once, and each move doubles.
BUSTER_GLOBAL_LOCAL u64 c_ast_test_column_moves(u64 capacity, u64 nodes)
{
    u64 result = 0;
    for (u64 held = capacity; held < nodes; held *= 2)
    {
        result += held * 13;
    }
    return result;
}

// The node columns are written once, in place, in arenas of their own:
// - trees on either side of every column step boundary build identically;
// - the phase arena holds only the builder's stacks, so its peak does not grow
//   with the tree;
// - a tree that outgrows its reservations moves byte for byte, and the move is
//   the only copy counted;
// - c_ast_release returns every reservation, through any copy, and a failed
//   or refused build leaves none behind.
BUSTER_GLOBAL_LOCAL UnitTestResult c_ast_test_columns(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    TemporalArena temporary = scratch_begin(&arguments->arena, 1);
    CPreprocessResult empty = c_ast_test_preprocess(temporary.arena, S8("void f(void) { int x; }"), C_PREPROCESS_DIALECT_GNU17);
    CAstResult empty_tree = c_ast_build(temporary.arena, empty, (CAstOptions){0});
    if (BUSTER_REQUIRE(arguments, empty_tree.complete && empty_tree.ast.storage))
    {
        u32 base = empty_tree.ast.node_count;
        u32 const sizes[] = {C_AST_COLUMN_STEP - 1, C_AST_COLUMN_STEP, C_AST_COLUMN_STEP + 1, 2 * C_AST_COLUMN_STEP, 3 * C_AST_COLUMN_STEP + 2};
        u64 small_transient = 0;
        for (u32 size = 0; size < BUSTER_ARRAY_LENGTH(sizes); size += 1)
        {
            CPreprocessResult preprocess = c_ast_test_preprocess(temporary.arena, c_ast_test_column_source(temporary.arena, base, sizes[size]),
                                                                  C_PREPROCESS_DIALECT_GNU17);
            CAstResult reference = c_ast_build(temporary.arena, preprocess, (CAstOptions){0});
            if (BUSTER_REQUIRE(arguments, preprocess.error_count == 0 && reference.complete && reference.ast.node_count == sizes[size]))
            {
                BUSTER_TEST(arguments, c_ast_validate(&reference.ast) == C_AST_NODE_INVALID);
                BUSTER_TEST(arguments, reference.statistics.column_copy_bytes == 0);
                BUSTER_TEST(arguments, reference.statistics.retained_bytes == (u64)sizes[size] * 13 + (u64)reference.ast.parenthesis_count * sizeof(CAstParenthesis));
                // Frames, stacks and bindings only: the same for every size.
                small_transient = size ? small_transient : reference.statistics.transient_high_water;
                BUSTER_TEST(arguments, reference.statistics.transient_high_water == small_transient);
                BUSTER_TEST(arguments, reference.statistics.transient_high_water * 16 < reference.statistics.retained_bytes);
                u32 const capacities[] = {1, 3, C_AST_COLUMN_STEP - 1, C_AST_COLUMN_STEP, C_AST_COLUMN_STEP + 1};
                for (u32 capacity = 0; capacity < BUSTER_ARRAY_LENGTH(capacities); capacity += 1)
                {
                    CAstResult moved = c_ast_build(temporary.arena, preprocess,
                                                   (CAstOptions){.column_capacity = capacities[capacity], .refill_batch = capacity});
                    BUSTER_TEST(arguments, moved.complete && c_ast_test_same_tree(&reference.ast, &moved.ast, false));
                    BUSTER_TEST(arguments, moved.statistics.column_copy_bytes == c_ast_test_column_moves(capacities[capacity], sizes[size]));
                    BUSTER_TEST(arguments, moved.statistics.transient_high_water == small_transient || capacity != 0);
                    c_ast_release(&moved.ast);
                }
            }
            c_ast_release(&reference.ast);
        }
        // Release returns every reservation and empties the tree, through any copy.
        u64 live = arena_test_live_reserved_bytes();
        CAstResult built = c_ast_build(temporary.arena, empty, (CAstOptions){0});
        BUSTER_TEST(arguments, built.complete && built.ast.storage && arena_test_live_reserved_bytes() > live);
        CAst copy = built.ast;
        c_ast_release(&built.ast);
        BUSTER_TEST(arguments, !built.ast.storage && !built.ast.kinds && built.ast.node_count == 0 && built.ast.root == C_AST_NODE_INVALID);
        BUSTER_TEST(arguments, arena_test_live_reserved_bytes() == live);
        c_ast_release(&copy);
        BUSTER_TEST(arguments, !copy.storage && copy.node_count == 0 && arena_test_live_reserved_bytes() == live);
        CAst none = {0};
        c_ast_release(&none);
        BUSTER_TEST(arguments, !none.storage && none.root == C_AST_NODE_INVALID);
        // A syntax error publishes no columns and keeps none.
        CPreprocessResult broken = c_ast_test_preprocess(temporary.arena, S8("void f(void) { int x }"), C_PREPROCESS_DIALECT_GNU17);
        live = arena_test_live_reserved_bytes();
        CAstResult failed = c_ast_build(temporary.arena, broken, (CAstOptions){.column_capacity = 1});
        BUSTER_TEST(arguments, !failed.complete && failed.diagnostic_count == 1 && !failed.ast.storage && failed.ast.node_count == 0);
        BUSTER_TEST(arguments, arena_test_live_reserved_bytes() == live);
        // A refused reservation fails the build with one diagnostic and keeps
        // nothing: the builder's own arena (1), the four columns (2 to 5) and
        // the four columns of the first move (6 to 9).
        for (u32 ordinal = 1; ordinal <= 9; ordinal += 1)
        {
            live = arena_test_live_reserved_bytes();
            c_test_fail_frontend_reservation(C_FRONTEND_RESERVATION_ANALYSIS, ordinal);
            CAstResult refused = c_ast_build(temporary.arena, empty, (CAstOptions){.column_capacity = 1});
            bool pending = c_test_frontend_reservation_pending();
            c_test_fail_frontend_reservation(C_FRONTEND_RESERVATION_ANALYSIS, 0);
            BUSTER_TEST(arguments, !pending && !refused.complete && !refused.ast.storage && refused.ast.node_count == 0);
            if (BUSTER_REQUIRE(arguments, refused.diagnostic_count == 1))
            {
                BUSTER_STRING_TEST(arguments, refused.diagnostics[0].message,
                                   ordinal == 1 ? S8("could not reserve the syntax tree builder arena") : S8("could not reserve the syntax tree columns"));
            }
            BUSTER_TEST(arguments, arena_test_live_reserved_bytes() == live);
        }
    }
    c_ast_release(&empty_tree.ast);
    c_test_scratch_end(temporary);
    return result;
}

// Tokens the intern pass never saw (symbol 0) are classified and bound by
// spelling and build the same tree.
BUSTER_GLOBAL_LOCAL UnitTestResult c_ast_test_uninterned(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    String8 extra[] = {
        S8("typedef int T; void f(void) { int T = 1; T * 2; T: ; } void (*g(int a))(T T) { T * p; } struct S { T T; };"),
        S8("typedef int T; T x; int h(a, b) T a; { return sizeof(T) + (T)b; } _Static_assert(1, \"m\"); int bool; int true = 1;"),
    };
    for (u32 program = 0; program < BUSTER_ARRAY_LENGTH(c_ast_test_programs) + BUSTER_ARRAY_LENGTH(extra); program += 1)
    {
        String8 text = program < BUSTER_ARRAY_LENGTH(c_ast_test_programs) ? c_ast_test_programs[program]
                                                                          : extra[program - BUSTER_ARRAY_LENGTH(c_ast_test_programs)];
        TemporalArena temporary = scratch_begin(&arguments->arena, 1);
        CPreprocessResult preprocess = c_ast_test_preprocess(temporary.arena, text, C_PREPROCESS_DIALECT_GNU17);
        if (BUSTER_REQUIRE(arguments, preprocess.error_count == 0))
        {
            CAstResult reference = c_ast_build(temporary.arena, preprocess, (CAstOptions){0});
            CToken* stripped = arena_allocate(temporary.arena, CToken, preprocess.token_count);
            memcpy(stripped, preprocess.tokens, preprocess.token_count * sizeof(CToken));
            for (u64 index = 0; index < preprocess.token_count; index += 1)
            {
                stripped[index].symbol = 0;
            }
            CPreprocessResult changed = preprocess;
            changed.tokens = stripped;
            CAstResult other = c_ast_build(temporary.arena, changed, (CAstOptions){0});
            // A hand-built stream with no symbol table at all: the builder
            // interns into a table of its own (c_symbol_table_standalone).
            CPreprocessResult tableless = changed;
            tableless.symbols = 0;
            CAstResult standalone = c_ast_build(temporary.arena, tableless, (CAstOptions){0});
            BUSTER_TEST(arguments, reference.complete && other.complete && standalone.complete);
            if (reference.complete && other.complete && standalone.complete)
            {
                BUSTER_TEST(arguments, c_ast_validate(&other.ast) == C_AST_NODE_INVALID);
                BUSTER_TEST(arguments, c_ast_validate(&standalone.ast) == C_AST_NODE_INVALID);
                String8 expected = c_ast_dump(temporary.arena, &reference.ast, preprocess, reference.ast.root);
                BUSTER_STRING_TEST(arguments, expected, c_ast_dump(temporary.arena, &other.ast, changed, other.ast.root));
                BUSTER_STRING_TEST(arguments, expected, c_ast_dump(temporary.arena, &standalone.ast, tableless, standalone.ast.root));
            }
            c_ast_release(&standalone.ast);
            c_ast_release(&other.ast);
            c_ast_release(&reference.ast);
        }
        c_test_scratch_end(temporary);
    }
    return result;
}

// ---- the declaration split from the tree ----------------------------------

// The first difference between c_parse_ast's result and the tree split's, or
// an empty string: the declaration and diagnostic counts and capacities,
// every field of every record, each record's body assertion ranges, and the
// diagnostics.
BUSTER_GLOBAL_LOCAL String8 c_ast_corpus_split_differ(Arena* arena, CParserResult const* walker, CParserResult const* tree)
{
    String8 difference = {0};
    if (walker->declaration_count != tree->declaration_count || walker->diagnostic_count != tree->diagnostic_count ||
        walker->declaration_capacity != tree->declaration_capacity || walker->diagnostic_capacity != tree->diagnostic_capacity ||
        (walker->number_facts == 0) != (tree->number_facts == 0))
    {
        difference = string_format(arena, S8("{u32} records, {u32} diagnostics from c_parse_ast; {u32}, {u32} from the tree split"), walker->declaration_count,
                                   walker->diagnostic_count, tree->declaration_count, tree->diagnostic_count);
    }
    u32 index = 0;
    CParserDeclaration const* right = tree->first_declaration;
    for (CParserDeclaration const* left = walker->first_declaration; left && right && !difference.length; left = left->next, right = right->next)
    {
#define C_AST_CORPUS_SPLIT_FIELD(field)                                                                                                         \
        if (!difference.length && (u64)left->field != (u64)right->field)                                                                        \
        {                                                                                                                                       \
            difference = string_format(arena, S8("record {u32} (token {u32}): " #field ": c_parse_ast {u64}, tree split {u64}"), index,          \
                                       left->token_start, (u64)left->field, (u64)right->field);                                                \
        }
        C_AST_CORPUS_SPLIT_FIELD(token_start)
        C_AST_CORPUS_SPLIT_FIELD(token_count)
        C_AST_CORPUS_SPLIT_FIELD(kind)
        C_AST_CORPUS_SPLIT_FIELD(declarator_start)
        C_AST_CORPUS_SPLIT_FIELD(declarator_count)
        C_AST_CORPUS_SPLIT_FIELD(body_start)
        C_AST_CORPUS_SPLIT_FIELD(body_token_count)
        C_AST_CORPUS_SPLIT_FIELD(identifier_list_start)
        C_AST_CORPUS_SPLIT_FIELD(identifier_list_token_count)
        C_AST_CORPUS_SPLIT_FIELD(parameter_declaration_start)
        C_AST_CORPUS_SPLIT_FIELD(parameter_declaration_token_count)
        C_AST_CORPUS_SPLIT_FIELD(name_token)
        C_AST_CORPUS_SPLIT_FIELD(function_name_token)
        C_AST_CORPUS_SPLIT_FIELD(expression.token_start)
        C_AST_CORPUS_SPLIT_FIELD(expression.token_count)
        C_AST_CORPUS_SPLIT_FIELD(is_definition)
        C_AST_CORPUS_SPLIT_FIELD(is_typedef)
        C_AST_CORPUS_SPLIT_FIELD(is_constexpr)
        C_AST_CORPUS_SPLIT_FIELD(is_variadic)
        C_AST_CORPUS_SPLIT_FIELD(seen_equal)
        C_AST_CORPUS_SPLIT_FIELD(is_declarator_continuation)
        C_AST_CORPUS_SPLIT_FIELD(is_identifier_list_definition)
#undef C_AST_CORPUS_SPLIT_FIELD
        u32 assertion = 0;
        CParserStaticAssert const* tree_assertion = right->first_static_assert;
        for (CParserStaticAssert const* walker_assertion = left->first_static_assert; !difference.length && (walker_assertion || tree_assertion);
             walker_assertion = walker_assertion ? walker_assertion->next : 0, tree_assertion = tree_assertion ? tree_assertion->next : 0)
        {
            if (!walker_assertion || !tree_assertion || walker_assertion->token_start != tree_assertion->token_start ||
                walker_assertion->token_count != tree_assertion->token_count)
            {
                difference = string_format(arena, S8("record {u32} (token {u32}): body assertion {u32}: c_parse_ast [{u32}, +{u32}], tree split [{u32}, +{u32}]"),
                                           index, left->token_start, assertion, walker_assertion ? walker_assertion->token_start : UINT32_MAX,
                                           walker_assertion ? walker_assertion->token_count : 0, tree_assertion ? tree_assertion->token_start : UINT32_MAX,
                                           tree_assertion ? tree_assertion->token_count : 0);
            }
            assertion += 1;
        }
        index += 1;
    }
    for (u32 diagnostic = 0; diagnostic < walker->diagnostic_count && !difference.length; diagnostic += 1)
    {
        CDiagnostic const* a = walker->diagnostics + diagnostic;
        CDiagnostic const* b = tree->diagnostics + diagnostic;
        if (!string_equal(a->message, b->message) || a->kind != b->kind || a->severity != b->severity || a->location.offset != b->location.offset ||
            a->location.line != b->location.line || a->location.column != b->location.column || a->location.file != b->location.file)
        {
            difference = string_format(arena, S8("diagnostic {u32} differs: '{S8}' from c_parse_ast, '{S8}' from the tree split"), diagnostic, a->message,
                                       b->message);
        }
    }
    return difference;
}

// The first difference between the bindings of two analyses: every
// identifier use (token, entity, scope) in order and the per-token use map,
// every scope, and every entity's identity, place and declaration range.
// Empty when there is none.
BUSTER_GLOBAL_LOCAL String8 c_ast_corpus_bindings_differ(Arena* arena, CAnalysisResult const* left, CAnalysisResult const* right)
{
    String8 difference = {0};
    if (left->identifier_use_count != right->identifier_use_count || left->scope_count != right->scope_count ||
        left->entity_count != right->entity_count || left->identifier_use_by_token_capacity != right->identifier_use_by_token_capacity)
    {
        difference = string_format(arena, S8("{u32} uses, {u32} scopes, {u32} entities without the tree; {u32}, {u32}, {u32} with it"),
                                   left->identifier_use_count, left->scope_count, left->entity_count, right->identifier_use_count, right->scope_count,
                                   right->entity_count);
    }
    for (u32 index = 0; index < left->identifier_use_count && !difference.length; index += 1)
    {
        CIdentifierUse a = left->identifier_uses[index];
        CIdentifierUse b = right->identifier_uses[index];
        if (a.token_index != b.token_index || a.entity.value != b.entity.value || a.scope.value != b.scope.value)
        {
            difference = string_format(arena,
                                       S8("use {u32} differs: token {u32} entity {u32} scope {u32} without the tree, "
                                          "token {u32} entity {u32} scope {u32} with it"),
                                       index, a.token_index, a.entity.value, a.scope.value, b.token_index, b.entity.value, b.scope.value);
        }
    }
    for (u32 index = 0; left->identifier_use_by_token_plus_one && index < left->identifier_use_by_token_capacity && !difference.length; index += 1)
    {
        if (left->identifier_use_by_token_plus_one[index] != right->identifier_use_by_token_plus_one[index])
        {
            difference = string_format(arena, S8("token {u32} maps to use {u32} without the tree, {u32} with it"), index,
                                       left->identifier_use_by_token_plus_one[index], right->identifier_use_by_token_plus_one[index]);
        }
    }
    for (u32 index = 0; index < left->scope_count && !difference.length; index += 1)
    {
        CScope a = left->scopes[index];
        CScope b = right->scopes[index];
        if (a.parent.value != b.parent.value || a.first_entity.value != b.first_entity.value || a.last_entity.value != b.last_entity.value ||
            a.token_start != b.token_start || a.token_end != b.token_end || a.entity_count != b.entity_count)
        {
            difference = string_format(arena, S8("scope {u32} differs: [{u32}, {u32}) without the tree, [{u32}, {u32}) with it"), index, a.token_start,
                                       a.token_end, b.token_start, b.token_end);
        }
    }
    for (u32 index = 0; index < left->entity_count && !difference.length; index += 1)
    {
        CEntity const* a = left->entities + index;
        CEntity const* b = right->entities + index;
        if (!string_equal(a->name, b->name) || a->type.value != b->type.value || a->scope.value != b->scope.value || a->kind != b->kind ||
            a->next_in_scope.value != b->next_in_scope.value || a->declaration_index != b->declaration_index ||
            a->declaration_token_plus_one != b->declaration_token_plus_one || a->declaration_statement_start != b->declaration_statement_start ||
            a->declaration_token_start != b->declaration_token_start || a->declaration_token_count != b->declaration_token_count ||
            a->is_definition != b->is_definition || a->is_static_storage != b->is_static_storage || a->is_extern != b->is_extern)
        {
            difference = string_format(arena, S8("entity {u32} ('{S8}') differs"), index, a->name);
        }
    }
    return difference;
}

// The first difference between two analyses: the sizes of the type tables,
// then the diagnostics (count, then each message, kind, severity and
// location). Empty when there is none.
BUSTER_GLOBAL_LOCAL String8 c_ast_corpus_analyses_differ(Arena* arena, CAnalysisResult const* left, CAnalysisResult const* right)
{
    String8 difference = {0};
    if (left->type_count != right->type_count || left->array_bound_count != right->array_bound_count || left->member_count != right->member_count ||
        left->entity_count != right->entity_count)
    {
        difference = string_format(arena, S8("tables differ: {u32} types, {u32} bounds, {u32} members, {u32} entities without the tree; "
                                             "{u32}, {u32}, {u32}, {u32} with it"),
                                   left->type_count, left->array_bound_count, left->member_count, left->entity_count, right->type_count,
                                   right->array_bound_count, right->member_count, right->entity_count);
    }
    else if (left->diagnostic_count != right->diagnostic_count || left->analysis_complete != right->analysis_complete)
    {
        difference = string_format(arena, S8("{u32} diagnostics (complete {u32}) without the tree, {u32} (complete {u32}) with it"), left->diagnostic_count,
                                   (u32)left->analysis_complete, right->diagnostic_count, (u32)right->analysis_complete);
    }
    for (u32 index = 0; index < left->diagnostic_count && !difference.length; index += 1)
    {
        CDiagnostic const* a = left->diagnostics + index;
        CDiagnostic const* b = right->diagnostics + index;
        if (!string_equal(a->message, b->message) || a->kind != b->kind || a->severity != b->severity || a->location.offset != b->location.offset ||
            a->location.line != b->location.line || a->location.column != b->location.column || a->location.file != b->location.file)
        {
            difference = string_format(arena, S8("diagnostic {u32} differs: '{S8}' without the tree, '{S8}' with it"), index, a->message, b->message);
        }
    }
    return difference;
}


// The analysis model field by field: every row of the type, entity, scope,
// array-bound, alignment, declaration, identifier-use, member, enum-member
// and parameter tables, an enumerator's declaration-point integer constant
// field by field. Names are compared by content; the syntax record pointer
// and padding are not compared, since each analysis parses its own. The counts are compared
// first (c_ast_corpus_analyses_differ), so the rows line up. `rows` receives
// the number of rows compared.
#define C_AST_MODEL_FIELD(table, field) \
    (difference.length || left->table[index].field == right->table[index].field ? difference \
                                                                                 : string_format(arena, S8(#table " {u32} differs in " #field), index))
#define C_AST_MODEL_NAME(table, field) \
    (difference.length || string_equal(left->table[index].field, right->table[index].field) \
         ? difference \
         : string_format(arena, S8(#table " {u32} differs in " #field), index))
#define C_AST_MODEL_SITE(table, field) \
    (difference.length || (left->table[index].field.map_offset_plus_one == right->table[index].field.map_offset_plus_one && \
                           left->table[index].field.file == right->table[index].field.file) \
         ? difference \
         : string_format(arena, S8(#table " {u32} differs in " #field), index))

BUSTER_GLOBAL_LOCAL String8 c_ast_corpus_models_differ(Arena* arena, CAnalysisResult const* left, CAnalysisResult const* right, u64* rows)
{
    String8 difference = {0};
    if (left->declaration_count != right->declaration_count || left->parameter_count != right->parameter_count ||
        left->enum_member_count != right->enum_member_count || left->alignment_count != right->alignment_count ||
        left->scope_count != right->scope_count || left->identifier_use_count != right->identifier_use_count)
    {
        difference = string_format(arena, S8("model tables differ: {u32} declarations, {u32} parameters, {u32} enum members, {u32} alignments, {u32} scopes, "
                                             "{u32} uses without the tree; {u32}, {u32}, {u32}, {u32}, {u32}, {u32} with it"),
                                   left->declaration_count, left->parameter_count, left->enum_member_count, left->alignment_count, left->scope_count,
                                   left->identifier_use_count, right->declaration_count, right->parameter_count, right->enum_member_count,
                                   right->alignment_count, right->scope_count, right->identifier_use_count);
    }
    for (u32 index = 0; index < left->type_count && !difference.length; index += 1)
    {
        difference = C_AST_MODEL_NAME(types, tag);
        difference = C_AST_MODEL_FIELD(types, tag_scope.value);
        difference = C_AST_MODEL_FIELD(types, element_type.value);
        difference = C_AST_MODEL_FIELD(types, return_type.value);
        difference = C_AST_MODEL_FIELD(types, unqualified_type.value);
        difference = C_AST_MODEL_FIELD(types, array_bound);
        difference = C_AST_MODEL_FIELD(types, parameter_start);
        difference = C_AST_MODEL_FIELD(types, parameter_count);
        difference = C_AST_MODEL_FIELD(types, member_start);
        difference = C_AST_MODEL_FIELD(types, member_count);
        difference = C_AST_MODEL_FIELD(types, enum_member_start);
        difference = C_AST_MODEL_FIELD(types, enum_member_count);
        difference = C_AST_MODEL_FIELD(types, definition_start);
        difference = C_AST_MODEL_FIELD(types, definition_token_count);
        difference = C_AST_MODEL_FIELD(types, vector_byte_size);
        difference = C_AST_MODEL_FIELD(types, kind);
        difference = C_AST_MODEL_FIELD(types, is_const);
        difference = C_AST_MODEL_FIELD(types, is_volatile);
        difference = C_AST_MODEL_FIELD(types, is_restrict);
        difference = C_AST_MODEL_FIELD(types, is_atomic);
        difference = C_AST_MODEL_FIELD(types, is_variadic);
        difference = C_AST_MODEL_FIELD(types, is_complete);
        difference = C_AST_MODEL_FIELD(types, is_transparent_union);
        difference = C_AST_MODEL_FIELD(types, has_unqualified_type);
        difference = C_AST_MODEL_FIELD(types, is_unprototyped);
        difference = C_AST_MODEL_FIELD(types, has_fixed_underlying_type);
    }
    for (u32 index = 0; index < left->entity_count && !difference.length; index += 1)
    {
        difference = C_AST_MODEL_NAME(entities, name);
        difference = C_AST_MODEL_FIELD(entities, symbol);
        difference = C_AST_MODEL_SITE(entities, location);
        difference = C_AST_MODEL_FIELD(entities, type.value);
        difference = C_AST_MODEL_FIELD(entities, scope.value);
        difference = C_AST_MODEL_FIELD(entities, next_in_scope.value);
        difference = C_AST_MODEL_FIELD(entities, next_in_lookup.value);
        difference = C_AST_MODEL_FIELD(entities, next_typedef_in_lookup.value);
        difference = C_AST_MODEL_FIELD(entities, next_by_name.value);
        difference = C_AST_MODEL_FIELD(entities, declaration_index);
        difference = C_AST_MODEL_FIELD(entities, declaration_token_plus_one);
        difference = C_AST_MODEL_FIELD(entities, declaration_statement_start);
        difference = C_AST_MODEL_FIELD(entities, declaration_token_start);
        difference = C_AST_MODEL_FIELD(entities, declaration_token_count);
        difference = C_AST_MODEL_FIELD(entities, alignment_start);
        difference = C_AST_MODEL_FIELD(entities, alignment_count);
        difference = C_AST_MODEL_FIELD(entities, kind);
        difference = C_AST_MODEL_FIELD(entities, is_definition);
        difference = C_AST_MODEL_FIELD(entities, is_static_storage);
        difference = C_AST_MODEL_FIELD(entities, is_thread_local);
        difference = C_AST_MODEL_FIELD(entities, is_constexpr);
        difference = C_AST_MODEL_FIELD(entities, has_constant_value);
        difference = C_AST_MODEL_FIELD(entities, constant_is_negative);
        difference = C_AST_MODEL_FIELD(entities, has_cleanup);
        difference = C_AST_MODEL_FIELD(entities, cleanup_attribute_checked);
        difference = C_AST_MODEL_FIELD(entities, is_register);
        difference = C_AST_MODEL_FIELD(entities, is_extern);
        difference = C_AST_MODEL_FIELD(entities, definition_is_gnu_inline_only);
        difference = C_AST_MODEL_FIELD(entities, has_internal_linkage);
        difference = C_AST_MODEL_FIELD(entities, inline_hints);
        difference = C_AST_MODEL_FIELD(entities, cleanup_function.value);
        difference = C_AST_MODEL_FIELD(entities, cleanup_attribute_token);
        difference = C_AST_MODEL_FIELD(entities, cleanup_attribute_end);
        difference = C_AST_MODEL_FIELD(entities, enum_member_plus_one);
        difference = C_AST_MODEL_FIELD(entities, definition_parameter_count_plus_one);
        difference = C_AST_MODEL_FIELD(entities, prototype_parameter_count_plus_one);
        difference = C_AST_MODEL_FIELD(entities, constant_value);
    }
    for (u32 index = 0; index < left->scope_count && !difference.length; index += 1)
    {
        difference = C_AST_MODEL_FIELD(scopes, parent.value);
        difference = C_AST_MODEL_FIELD(scopes, first_entity.value);
        difference = C_AST_MODEL_FIELD(scopes, last_entity.value);
        difference = C_AST_MODEL_FIELD(scopes, token_start);
        difference = C_AST_MODEL_FIELD(scopes, token_end);
        difference = C_AST_MODEL_FIELD(scopes, entity_count);
    }
    for (u32 index = 0; index < left->array_bound_count && !difference.length; index += 1)
    {
        difference = C_AST_MODEL_FIELD(array_bounds, inferred_count);
        difference = C_AST_MODEL_FIELD(array_bounds, token_start);
        difference = C_AST_MODEL_FIELD(array_bounds, token_count);
        difference = C_AST_MODEL_FIELD(array_bounds, is_static);
        difference = C_AST_MODEL_FIELD(array_bounds, is_star);
        difference = C_AST_MODEL_FIELD(array_bounds, has_inferred_count);
        difference = C_AST_MODEL_FIELD(array_bounds, is_const);
        difference = C_AST_MODEL_FIELD(array_bounds, is_parameter_declarator);
    }
    for (u32 index = 0; index < left->alignment_count && !difference.length; index += 1)
    {
        difference = C_AST_MODEL_FIELD(alignments, type.value);
        difference = C_AST_MODEL_FIELD(alignments, token_start);
        difference = C_AST_MODEL_FIELD(alignments, token_count);
    }
    for (u32 index = 0; index < left->declaration_count && !difference.length; index += 1)
    {
        difference = C_AST_MODEL_NAME(declarations, name);
        difference = C_AST_MODEL_SITE(declarations, location);
        difference = C_AST_MODEL_FIELD(declarations, token_start);
        difference = C_AST_MODEL_FIELD(declarations, token_count);
        difference = C_AST_MODEL_FIELD(declarations, declarator_start);
        difference = C_AST_MODEL_FIELD(declarations, declarator_count);
        difference = C_AST_MODEL_FIELD(declarations, body_start);
        difference = C_AST_MODEL_FIELD(declarations, body_token_count);
        difference = C_AST_MODEL_FIELD(declarations, identifier_list_start);
        difference = C_AST_MODEL_FIELD(declarations, identifier_list_token_count);
        difference = C_AST_MODEL_FIELD(declarations, parameter_declaration_start);
        difference = C_AST_MODEL_FIELD(declarations, parameter_declaration_token_count);
        difference = C_AST_MODEL_FIELD(declarations, parameter_start);
        difference = C_AST_MODEL_FIELD(declarations, parameter_count);
        difference = C_AST_MODEL_FIELD(declarations, alignment_start);
        difference = C_AST_MODEL_FIELD(declarations, alignment_count);
        difference = C_AST_MODEL_FIELD(declarations, type.value);
        difference = C_AST_MODEL_FIELD(declarations, base_type.value);
        difference = C_AST_MODEL_FIELD(declarations, entity.value);
        difference = C_AST_MODEL_FIELD(declarations, scope.value);
        difference = C_AST_MODEL_FIELD(declarations, kind);
        difference = C_AST_MODEL_FIELD(declarations, is_definition);
        difference = C_AST_MODEL_FIELD(declarations, is_variadic);
        difference = C_AST_MODEL_FIELD(declarations, is_constexpr);
        difference = C_AST_MODEL_FIELD(declarations, is_identifier_list_definition);
        difference = C_AST_MODEL_FIELD(declarations, is_gnu_inline_only);
        difference = C_AST_MODEL_FIELD(declarations, is_declarator_continuation);
    }
    for (u32 index = 0; index < left->identifier_use_count && !difference.length; index += 1)
    {
        difference = C_AST_MODEL_FIELD(identifier_uses, token_index);
        difference = C_AST_MODEL_FIELD(identifier_uses, entity.value);
        difference = C_AST_MODEL_FIELD(identifier_uses, scope.value);
    }
    for (u32 index = 0; index < left->member_count && !difference.length; index += 1)
    {
        difference = C_AST_MODEL_NAME(members, name);
        difference = C_AST_MODEL_SITE(members, location);
        difference = C_AST_MODEL_FIELD(members, symbol);
        difference = C_AST_MODEL_FIELD(members, type.value);
        difference = C_AST_MODEL_FIELD(members, alignment_start);
        difference = C_AST_MODEL_FIELD(members, alignment_count);
        difference = C_AST_MODEL_FIELD(members, bit_width);
        difference = C_AST_MODEL_FIELD(members, bit_width_token_start);
        difference = C_AST_MODEL_FIELD(members, bit_width_token_count);
        difference = C_AST_MODEL_FIELD(members, is_bit_field);
        difference = C_AST_MODEL_FIELD(members, is_packed);
        difference = C_AST_MODEL_FIELD(members, bit_width_resolved);
        difference = C_AST_MODEL_FIELD(members, has_incomplete_type);
    }
    for (u32 index = 0; index < left->enum_member_count && !difference.length; index += 1)
    {
        difference = C_AST_MODEL_NAME(enum_members, name);
        difference = C_AST_MODEL_SITE(enum_members, location);
        difference = C_AST_MODEL_FIELD(enum_members, symbol);
        difference = C_AST_MODEL_FIELD(enum_members, token_index);
        difference = C_AST_MODEL_FIELD(enum_members, enum_type.value);
        difference = C_AST_MODEL_FIELD(enum_members, declaration_type.value);
        difference = C_AST_MODEL_FIELD(enum_members, type.value);
        difference = C_AST_MODEL_FIELD(enum_members, integer_constant.magnitude);
        difference = C_AST_MODEL_FIELD(enum_members, integer_constant.magnitude_high);
        difference = C_AST_MODEL_FIELD(enum_members, integer_constant.type.value);
        difference = C_AST_MODEL_FIELD(enum_members, integer_constant.rank);
        difference = C_AST_MODEL_FIELD(enum_members, integer_constant.bit_width);
        difference = C_AST_MODEL_FIELD(enum_members, integer_constant.is_signed);
        difference = C_AST_MODEL_FIELD(enum_members, integer_constant.is_negative);
        difference = C_AST_MODEL_FIELD(enum_members, integer_constant.valid);
        difference = C_AST_MODEL_FIELD(enum_members, value);
        difference = C_AST_MODEL_FIELD(enum_members, is_negative);
        difference = C_AST_MODEL_FIELD(enum_members, is_published);
        difference = C_AST_MODEL_FIELD(enum_members, is_prototype_scope);
        difference = C_AST_MODEL_FIELD(enum_members, is_expression_defined);
    }
    for (u32 index = 0; index < left->parameter_count && !difference.length; index += 1)
    {
        difference = C_AST_MODEL_NAME(parameters, name);
        difference = C_AST_MODEL_SITE(parameters, location);
        difference = C_AST_MODEL_FIELD(parameters, type.value);
        difference = C_AST_MODEL_FIELD(parameters, entity.value);
        difference = C_AST_MODEL_FIELD(parameters, symbol);
    }
    *rows += (u64)left->type_count + left->entity_count + left->scope_count + left->array_bound_count + left->alignment_count + left->declaration_count +
             left->identifier_use_count + left->member_count + left->enum_member_count + left->parameter_count;
    return difference;
}

#undef C_AST_MODEL_SITE
#undef C_AST_MODEL_NAME
#undef C_AST_MODEL_FIELD

// The analysis model, field by field: every type row, array bound,
// alignment specifier, type alignment, member, parameter, entity and
// declaration's types. The declaration specifier read (c_parse.c, #3102)
// must leave every table exactly as the type machine builds it, in the same
// order; the diagnostics are compared by c_ast_corpus_analyses_differ. Node
// provenance is checked independently: the token model has no node ids.
BUSTER_GLOBAL_LOCAL String8 c_ast_corpus_model_differ(Arena* arena, CAnalysisResult const* left, CAnalysisResult const* right)
{
    String8 difference = {0};
    if (left->type_count != right->type_count || left->array_bound_count != right->array_bound_count ||
        left->alignment_count != right->alignment_count || left->type_alignment_count != right->type_alignment_count ||
        left->member_count != right->member_count || left->parameter_count != right->parameter_count || left->entity_count != right->entity_count ||
        left->declaration_count != right->declaration_count)
    {
        difference = string_format(arena,
                                   S8("tables differ: {u32} types, {u32} bounds, {u32} alignments, {u32} type alignments, {u32} members, {u32} parameters, "
                                      "{u32} entities, {u32} declarations without the tree; {u32}, {u32}, {u32}, {u32}, {u32}, {u32}, {u32}, {u32} with it"),
                                   left->type_count, left->array_bound_count, left->alignment_count, left->type_alignment_count, left->member_count,
                                   left->parameter_count, left->entity_count, left->declaration_count, right->type_count, right->array_bound_count,
                                   right->alignment_count, right->type_alignment_count, right->member_count, right->parameter_count, right->entity_count,
                                   right->declaration_count);
    }
    for (u32 index = 0; index < left->type_count && !difference.length; index += 1)
    {
        CType const* a = left->types + index;
        CType const* b = right->types + index;
        bool same = string_equal(a->tag, b->tag) && a->tag_scope.value == b->tag_scope.value && a->element_type.value == b->element_type.value &&
                    a->return_type.value == b->return_type.value && a->unqualified_type.value == b->unqualified_type.value &&
                    a->array_bound == b->array_bound && a->parameter_start == b->parameter_start && a->parameter_count == b->parameter_count &&
                    a->member_start == b->member_start && a->member_count == b->member_count && a->enum_member_start == b->enum_member_start &&
                    a->enum_member_count == b->enum_member_count && a->definition_start == b->definition_start &&
                    a->definition_token_count == b->definition_token_count && a->vector_byte_size == b->vector_byte_size && a->kind == b->kind &&
                    a->is_const == b->is_const && a->is_volatile == b->is_volatile && a->is_restrict == b->is_restrict && a->is_atomic == b->is_atomic &&
                    a->is_variadic == b->is_variadic && a->is_complete == b->is_complete && a->is_transparent_union == b->is_transparent_union &&
                    a->has_unqualified_type == b->has_unqualified_type && a->is_unprototyped == b->is_unprototyped &&
                    a->has_fixed_underlying_type == b->has_fixed_underlying_type;
        if (!same)
        {
            difference = string_format(arena, S8("type {u32} differs: kind {u32} element {u32} without the tree, kind {u32} element {u32} with it"), index,
                                       (u32)a->kind, a->element_type.value, (u32)b->kind, b->element_type.value);
        }
    }
    for (u32 index = 0; index < left->array_bound_count && !difference.length; index += 1)
    {
        CArrayBound const* a = left->array_bounds + index;
        CArrayBound const* b = right->array_bounds + index;
        if (a->inferred_count != b->inferred_count || a->token_start != b->token_start || a->token_count != b->token_count ||
            a->is_static != b->is_static || a->is_star != b->is_star || a->has_inferred_count != b->has_inferred_count || a->is_const != b->is_const ||
            a->is_parameter_declarator != b->is_parameter_declarator)
        {
            difference = string_format(arena, S8("array bound {u32} differs"), index);
        }
    }
    for (u32 index = 0; index < left->alignment_count && !difference.length; index += 1)
    {
        CAlignmentSpecifier const* a = left->alignments + index;
        CAlignmentSpecifier const* b = right->alignments + index;
        if (a->type.value != b->type.value || a->token_start != b->token_start || a->token_count != b->token_count)
        {
            difference = string_format(arena, S8("alignment {u32} differs"), index);
        }
    }
    for (u32 index = 0; index < left->type_alignment_count && !difference.length; index += 1)
    {
        CTypeAlignment const* a = left->type_alignments + index;
        CTypeAlignment const* b = right->type_alignments + index;
        if (a->type_index != b->type_index || a->alignment_start != b->alignment_start || a->alignment_count != b->alignment_count)
        {
            difference = string_format(arena, S8("type alignment {u32} differs"), index);
        }
    }
    for (u32 index = 0; index < left->member_count && !difference.length; index += 1)
    {
        CMember const* a = left->members + index;
        CMember const* b = right->members + index;
        if (a->type.value != b->type.value || a->symbol != b->symbol || a->alignment_start != b->alignment_start ||
            a->alignment_count != b->alignment_count || a->bit_width != b->bit_width || a->is_bit_field != b->is_bit_field ||
            a->is_packed != b->is_packed || !string_equal(a->name, b->name))
        {
            difference = string_format(arena, S8("member {u32} differs"), index);
        }
    }
    for (u32 index = 0; index < left->parameter_count && !difference.length; index += 1)
    {
        CParameter const* a = left->parameters + index;
        CParameter const* b = right->parameters + index;
        if (a->type.value != b->type.value || a->entity.value != b->entity.value || a->symbol != b->symbol || !string_equal(a->name, b->name))
        {
            difference = string_format(arena, S8("parameter {u32} differs"), index);
        }
    }
    for (u32 index = 0; index < left->entity_count && !difference.length; index += 1)
    {
        CEntity const* a = left->entities + index;
        CEntity const* b = right->entities + index;
        if (a->type.value != b->type.value || a->scope.value != b->scope.value || a->kind != b->kind || a->symbol != b->symbol ||
            a->declaration_index != b->declaration_index || a->alignment_start != b->alignment_start || a->alignment_count != b->alignment_count ||
            a->is_definition != b->is_definition || a->is_static_storage != b->is_static_storage || a->is_thread_local != b->is_thread_local ||
            a->is_constexpr != b->is_constexpr || a->is_register != b->is_register || a->is_extern != b->is_extern)
        {
            difference = string_format(arena, S8("entity {u32} ('{S8}') differs: type {u32} without the tree, {u32} with it"), index, a->name,
                                       a->type.value, b->type.value);
        }
    }
    for (u32 index = 0; index < left->declaration_count && !difference.length; index += 1)
    {
        CDeclaration const* a = left->declarations + index;
        CDeclaration const* b = right->declarations + index;
        if (a->type.value != b->type.value || a->base_type.value != b->base_type.value || a->entity.value != b->entity.value ||
            a->scope.value != b->scope.value || a->kind != b->kind || a->alignment_start != b->alignment_start || a->alignment_count != b->alignment_count)
        {
            difference = string_format(arena, S8("declaration {u32} differs: type {u32} without the tree, {u32} with it"), index, a->type.value,
                                       b->type.value);
        }
    }
    return difference;
}

// AST provenance is absent from the token-reference model above. Every named
// file-scope declaration nevertheless needs one independently checked owner
// before lowering can consume these links without reconstructing source.
BUSTER_GLOBAL_LOCAL bool c_ast_test_declaration_owners_consistent(CAnalysisResult const* analysis, CAst const* ast)
{
    bool valid = true;
    for (u32 index = 0; index < analysis->declaration_count && valid; index += 1)
    {
        CDeclaration const* declaration = analysis->declarations + index;
        u32 owner = declaration->declaration_node_plus_one - 1;
        u32 init = declaration->init_declarator_plus_one - 1;
        u32 initializer = declaration->initializer_node_plus_one - 1;
        valid = owner < ast->node_count && ast->tokens[owner] <= declaration->token_start;
        bool named = declaration->name.length && declaration->kind != C_DECLARATION_TYPE && declaration->kind != C_DECLARATION_ASSEMBLY;
        bool definition = declaration->kind == C_DECLARATION_FUNCTION && declaration->is_definition;
        if (valid && named && !definition && ast->kinds[owner] == C_AST_DECLARATION && c_ast_list_count(ast, owner))
        {
            valid = init < ast->node_count && ast->kinds[init] == C_AST_INIT_DECLARATOR &&
                    c_ast_subtree_begin(ast, owner) <= c_ast_subtree_begin(ast, init) && init < owner;
        }
        if (valid && init < ast->node_count)
        {
            valid = ((ast->data[init] & 4u) != 0) == (initializer < ast->node_count) &&
                    (initializer == C_AST_NODE_INVALID || initializer + 1 == init);
        }
    }
    return valid;
}

// The matching-delimiter index c_ast_spans_build reads (closer + 1 at each
// opener, 0 elsewhere), built with a stack as CPositionIndex builds it. A
// stream whose delimiters do not match has none.
BUSTER_GLOBAL_LOCAL u32* c_ast_test_matches(Arena* arena, CPreprocessResult preprocess)
{
    u32 count = (u32)preprocess.token_count;
    u32* matches = arena_allocate_zeroed(arena, u32, (u64)count + 1);
    u32* stack = arena_allocate(arena, u32, (u64)count + 1);
    u32 depth = 0;
    bool matched = true;
    for (u32 index = 0; index < count && matched; index += 1)
    {
        CToken const* token = &preprocess.tokens[index];
        bool open = token->punctuator == C_PUNCTUATOR_LEFT_PARENTHESIS || token->punctuator == C_PUNCTUATOR_LEFT_BRACKET ||
                    token->punctuator == C_PUNCTUATOR_LEFT_BRACE;
        bool close = token->punctuator == C_PUNCTUATOR_RIGHT_PARENTHESIS || token->punctuator == C_PUNCTUATOR_RIGHT_BRACKET ||
                     token->punctuator == C_PUNCTUATOR_RIGHT_BRACE;
        if (open)
        {
            stack[depth] = index;
            depth += 1;
        }
        else if (close)
        {
            matched = depth > 0;
            if (matched)
            {
                depth -= 1;
                matches[stack[depth]] = index + 1;
            }
        }
    }
    return matched && !depth ? matches : 0;
}

// The spellings of [first, end), one space apart.
BUSTER_GLOBAL_LOCAL String8 c_ast_test_span_text(Arena* arena, CPreprocessResult preprocess, u32 first, u32 end)
{
    String8 text = {0};
    for (u32 token = first; token < end; token += 1)
    {
        String8 spelling = c_token_spelling(preprocess.spelling_base, preprocess.tokens[token]);
        text = text.length ? string_format(arena, S8("{S8} {S8}"), text, spelling) : spelling;
    }
    return text;
}

// Every spanned node of a built tree holds its anchor token, and the span
// maps back to a node with the same span.
BUSTER_GLOBAL_LOCAL bool c_ast_test_spans_consistent(CAst const* ast, CAstSpans const* spans, CPreprocessResult preprocess, u32 const* matches)
{
    bool consistent = spans->node_count == ast->node_count;
    for (u32 node = 0; node < ast->node_count && consistent; node += 1)
    {
        u32 first = spans->first[node];
        u32 end = spans->end[node];
        if (first != C_AST_SPAN_NONE)
        {
            u32 token = ast->tokens[node];
            // A label address is anchored at its label, after the `&&`.
            consistent = token >= first && token < end;
            u32 found = c_ast_spans_locate(spans, preprocess.tokens, matches, first, end);
            consistent = consistent && found != C_AST_NODE_INVALID && spans->first[found] == first && spans->end[found] == end;
        }
    }
    return consistent;
}

#if !BUSTER_ANDROID && !BUSTER_IOS
// Mobile test runs carry no repository tree, so the fixture-reading suites
// below run only where the tests/ directory exists, as c_test.c's do.
// ---- fixtures -------------------------------------------------------------

// Repository fixtures the earlier syntax pass accepts must build a valid tree.
BUSTER_GLOBAL_LOCAL String8 const c_ast_test_fixtures[] = {
    S8_INITIALIZER("tests/basic_c_compile.c"),
    S8_INITIALIZER("tests/basic_c_operations.c"),
    S8_INITIALIZER("tests/basic_c_generic.c"),
    S8_INITIALIZER("tests/basic_c_c23_attributes.c"),
    S8_INITIALIZER("tests/basic_c_packed_layout.c"),
    S8_INITIALIZER("tests/basic_c_asm.c"),
    S8_INITIALIZER("tests/basic_c_asm_goto_range.c"),
    S8_INITIALIZER("tests/basic_c_atomic_specifier.c"),
    S8_INITIALIZER("tests/basic_c_auto_type.c"),
    S8_INITIALIZER("tests/basic_c_alignas.c"),
    S8_INITIALIZER("tests/basic_c_case_range.c"),
    S8_INITIALIZER("tests/basic_c_cleanup.c"),
    S8_INITIALIZER("tests/basic_c_bit_field_layout.c"),
    S8_INITIALIZER("tests/basic_c_builtin_math.c"),
    S8_INITIALIZER("tests/basic_c_compound_literal_type.c"),
    S8_INITIALIZER("tests/basic_c_designated_subscript_initializer.c"),
    S8_INITIALIZER("tests/basic_c_enum_prototype.c"),
    S8_INITIALIZER("tests/basic_c_fixed_enum.c"),
    S8_INITIALIZER("tests/basic_c_function_pointer_declarators.c"),
    S8_INITIALIZER("tests/basic_c_labels.c"),
    S8_INITIALIZER("tests/basic_c_llvm_switch.c"),
    S8_INITIALIZER("tests/basic_c_local_enum_declarator.c"),
    S8_INITIALIZER("tests/basic_c_nested_control_substatement.c"),
    S8_INITIALIZER("tests/basic_c_nested_string_initializers.c"),
    S8_INITIALIZER("tests/basic_c_pointer_to_vla.c"),
    S8_INITIALIZER("tests/basic_c_sizeof_compound_literal.c"),
    S8_INITIALIZER("tests/basic_c_aarch64_abi_contract.c"),
    S8_INITIALIZER("tests/basic_c_aarch64_i128_abi.c"),
};

BUSTER_GLOBAL_LOCAL UnitTestResult c_ast_test_fixture_sweep(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    u32 built_count = 0;
    for (u32 fixture = 0; fixture < BUSTER_ARRAY_LENGTH(c_ast_test_fixtures); fixture += 1)
    {
        TemporalArena temporary = scratch_begin(&arguments->arena, 1);
        ByteSlice file = file_read(temporary.arena, c_ast_test_fixtures[fixture], (FileReadOptions){0});
        if (file.pointer && file.length)
        {
            String8 source = {.pointer = (char8*)file.pointer, .length = file.length};
            String8 includes[] = {S8("tests")};
            CPreprocessResult preprocess = c_preprocess(temporary.arena, source,
                                                        (CPreprocessOptions){
                                                            .source_path = c_ast_test_fixtures[fixture],
                                                            .include_paths = includes,
                                                            .include_path_count = 1,
                                                            .target = target_native,
                                                            .data_layout = target_data_layout(target_native),
                                                        });
            if (preprocess.error_count == 0)
            {
                CParserResult syntax = c_parse_ast_reference(temporary.arena, preprocess);
                CAnalysisResult analysis = c_analyze_semantics_only(temporary.arena, preprocess, syntax);
                CAstResult built = c_ast_build(temporary.arena, preprocess, (CAstOptions){0});
                if (!syntax.diagnostic_count && !analysis.diagnostic_count && analysis.analysis_complete)
                {
                    BUSTER_TEST_RAW(arguments, built.complete, c_ast_test_fixtures[fixture]);
                    BUSTER_TEST_RAW(arguments, !built.complete || c_ast_validate(&built.ast) == C_AST_NODE_INVALID, c_ast_test_fixtures[fixture]);
                    built_count += built.complete;
                }
                c_ast_release(&built.ast);
            }
        }
        c_test_scratch_end(temporary);
    }
    BUSTER_TEST(arguments, built_count + 8 >= BUSTER_ARRAY_LENGTH(c_ast_test_fixtures));
    return result;
}

// ---- corpus differential --------------------------------------------------
//
// c_ast_test_corpus holds the tree to real inputs: every tests/**/*.c file the
// preprocessor accepts, tests/basic_c_dialect.c in each dialect, a table of
// declaration shapes the corpus holds few of, and (on Linux) the compiler's
// own C frontend sources. Three claims are checked on each.
//
// 1. The tree never rejects what the earlier syntax pass accepts. A file whose
//    preprocessing reports errors is skipped (it has no token stream: system
//    headers are not on the search path, a fixture guards itself with `#error`
//    for another target, or it needs a -D); every other file must build a
//    complete, valid tree unless it is pinned in c_ast_corpus_pins with the
//    reason it is not valid input. A pin must stay true: a pinned file that
//    builds fails the fixture, so a pin cannot go stale.
// 2. The tree agrees with c_parse_ast on the top-level declaration split. Both
//    passes produce one record per top-level declarator; the records must agree
//    in count, order and, per record, in the fields below. The tree side is
//    derived from the tree alone (c_ast_corpus_records):
//
//      FUNCTION_DEFINITION        one record: kind FUNCTION, definition.
//      DECLARATION, declarators   one record per INIT_DECLARATOR: kind TYPEDEF
//                                 when the specifiers hold `typedef`; else
//                                 FUNCTION when the first derivation above the
//                                 declarator's name is a function (`f(int)`,
//                                 `*f(int)`, `(*f(int))(int)`, `(f)(int)`), else
//                                 OBJECT (`(*f)(int)` is a pointer object).
//                                 Definition iff an OBJECT has an initializer.
//      DECLARATION, no declarator one record: TYPEDEF with `typedef`; TYPE when
//                                 the specifiers are c_parse_type_only_declaration's
//                                 shape (see CAstCorpusSpecifiers); else
//                                 OBJECT or UNKNOWN, whichever c_parse_ast's
//                                 name heuristic lands on (exclusion: the
//                                 grammar has no counterpart, see kind_open).
//      STATIC_ASSERT              one record: kind STATIC_ASSERT.
//      ASM_TOP_LEVEL              one record: kind ASSEMBLY.
//      EMPTY_DECLARATION          one record: kind UNKNOWN (what c_parse_ast
//                                 emits for a stray `;`).
//      PRAGMA                     none (c_preprocess leaves pragma tokens out of the
//                                 final stream, so c_parse_ast never sees one).
//
//    Compared per record: kind, is_definition, is_typedef, is_constexpr, the
//    first token of the external declaration (token_start), the declarator's
//    name token where the tree has a DECLARATOR_NAME (the function name for a
//    FUNCTION record, as every c_parse_ast consumer reads it), for a
//    definition the body start (c_parse_ast's body_start is the token after
//    the `{`; the tree's COMPOUND_STATEMENT anchor is the `{`),
//    is_declarator_continuation (every declarator after the first of a
//    declaration), and that the external declarations tile the token stream:
//    each one ends where the next begins and the last ends at the end-of-file
//    token. Not compared: declarator_start/declarator_count and the parameter
//    and identifier-list ranges (c_parse_ast's token-range bookkeeping, which
//    has no node counterpart), is_variadic (c_parse_ast reads it from the
//    parenthesis nesting of the declarator text; the tree states it as a
//    PARAMETER_LIST_VARIADIC on the derivation it belongs to), and the end of
//    a function body (covered by the tiling check).
//
//    Where the two passes legitimately differ, the input lives in
//    c_ast_corpus_known with the reason, and both sides are pinned. The table
//    is empty today.
// 3. The records c_parse_ast_from_tree publishes are c_parse_ast's: the same
//    count, every CParserDeclaration field of every record, each record's body
//    _Static_assert ranges, and the diagnostics (c_ast_corpus_split_differ).
//    A unit the split hands to the walker compares too. Floors on the records
//    it published itself keep a split that always falls back from passing,
//    and the frontend's own sources must take the split whole.

enum
{
    C_AST_CORPUS_PATH_LIMIT = 2048,
    C_AST_CORPUS_DIRECTORY_LIMIT = 32,
};

typedef struct CAstCorpusList CAstCorpusList;
struct CAstCorpusList
{
    String8* paths;
    String8* directories;
    u32 path_count;
    u32 directory_count;
    bool valid;
};

BUSTER_GLOBAL_LOCAL void c_ast_corpus_add_entry(Arena* arena, CAstCorpusList* list, String8 directory, String8 name)
{
    if (!string_equal(name, S8(".")) && !string_equal(name, S8("..")))
    {
        String8 path = string_format_z(arena, S8("{S8}/{S8}"), directory, name);
        if (string_ends_with_sequence(name, S8(".c")))
        {
            if (list->path_count < C_AST_CORPUS_PATH_LIMIT)
            {
                list->paths[list->path_count] = path;
                list->path_count += 1;
            }
            else
            {
                list->valid = false;
            }
        }
        else if (os_path_followed_stats(path).kind == OS_FILE_KIND_DIRECTORY)
        {
            if (list->directory_count < C_AST_CORPUS_DIRECTORY_LIMIT)
            {
                list->directories[list->directory_count] = path;
                list->directory_count += 1;
            }
            else
            {
                list->valid = false;
            }
        }
    }
}

BUSTER_GLOBAL_LOCAL void c_ast_corpus_list_directory(Arena* arena, CAstCorpusList* list, String8 directory)
{
#if BUSTER_WINDOWS
    String16 pattern = string16_from_string8(arena, string_format_z(arena, S8("{S8}\\*"), directory), true);
    WIN32_FIND_DATAW data;
    HANDLE find = FindFirstFileW(pattern.pointer, &data);
    bool more = find != INVALID_HANDLE_VALUE;
    list->valid = list->valid && more;
    while (more)
    {
        u64 length = 0;
        while (data.cFileName[length])
        {
            length += 1;
        }
        String16 name = {.pointer = (char16*)data.cFileName, .length = length};
        c_ast_corpus_add_entry(arena, list, directory, string8_from_string16(arena, name, false));
        more = FindNextFileW(find, &data) != 0;
    }
    if (find != INVALID_HANDLE_VALUE)
    {
        list->valid = list->valid && GetLastError() == ERROR_NO_MORE_FILES;
        FindClose(find);
    }
#else
    DIR* handle = opendir((const char*)directory.pointer);
    bool more = handle != 0;
    list->valid = list->valid && more;
    while (more)
    {
        errno = 0;
        struct dirent* entry = readdir(handle);
        more = entry != 0;
        if (entry)
        {
            c_ast_corpus_add_entry(arena, list, directory, string_from_pointer((const char8*)entry->d_name));
        }
        else
        {
            list->valid = list->valid && errno == 0;
        }
    }
    if (handle)
    {
        closedir(handle);
    }
#endif
}

BUSTER_GLOBAL_LOCAL bool c_ast_corpus_path_before(String8 left, String8 right)
{
    u64 common = BUSTER_MIN(left.length, right.length);
    int order = memcmp(left.pointer, right.pointer, common);
    return order < 0 || (order == 0 && left.length < right.length);
}

// Every tests/**/*.c path, sorted bytewise so the order does not depend on the
// directory's enumeration order. The directories are walked breadth first
// with the list itself as the worklist.
BUSTER_GLOBAL_LOCAL CAstCorpusList c_ast_corpus_collect(Arena* arena)
{
    CAstCorpusList list = {
        .paths = arena_allocate(arena, String8, C_AST_CORPUS_PATH_LIMIT),
        .directories = arena_allocate(arena, String8, C_AST_CORPUS_DIRECTORY_LIMIT),
        .valid = true,
    };
    list.directories[0] = string_duplicate_arena(arena, S8("tests"), true);
    list.directory_count = 1;
    for (u32 head = 0; head < list.directory_count; head += 1)
    {
        c_ast_corpus_list_directory(arena, &list, list.directories[head]);
    }
    for (u32 index = 1; index < list.path_count; index += 1)
    {
        String8 path = list.paths[index];
        u32 slot = index;
        while (slot > 0 && c_ast_corpus_path_before(path, list.paths[slot - 1]))
        {
            list.paths[slot] = list.paths[slot - 1];
            slot -= 1;
        }
        list.paths[slot] = path;
    }
    return list;
}

// ---- the declaration records ----

typedef struct CAstCorpusRecord CAstCorpusRecord;
struct CAstCorpusRecord
{
    u32 token_start;
    // The DECLARATOR_NAME token, or C_ID_UNDERLYING_INVALID when the construct
    // has no declarator.
    u32 name_token;
    // The token after a definition's `{`, else 0.
    u32 body_start;
    // The external declaration this record came from, and its position among
    // that declaration's declarators.
    u32 group;
    u32 declarator_index;
    CParserDeclarationKind kind;
    // The kind is OBJECT or UNKNOWN, whichever c_parse_ast's token heuristic
    // lands on (see c_ast_corpus_records).
    bool kind_open;
    bool is_definition;
    bool is_typedef;
    bool is_constexpr;
};

typedef struct CAstCorpusSpecifiers CAstCorpusSpecifiers;
struct CAstCorpusSpecifiers
{
    bool is_typedef;
    bool is_constexpr;
    // c_parse_type_only_declaration's shape: the decorations c_parse_skip_attributes
    // steps over (attribute lists, `__extension__`) and the qualifiers or storage
    // classes it skips (const, volatile, _Atomic, static, extern), then one
    // struct, union or enum specifier, then nothing else.
    bool type_only;
};

BUSTER_GLOBAL_LOCAL CAstCorpusSpecifiers c_ast_corpus_specifiers(CAst const* ast, u32 specifiers)
{
    CAstCorpusSpecifiers result = {0};
    bool seen_aggregate = false;
    bool other = false;
    u32 count = c_ast_child_count(ast, specifiers);
    for (u32 index = 0; index < count; index += 1)
    {
        u32 item = c_ast_child_at(ast, specifiers, index);
        CAstKind kind = (CAstKind)ast->kinds[item];
        bool word = kind == C_AST_SPECIFIER_WORD;
        bool aggregate = kind == C_AST_STRUCT_SPECIFIER || kind == C_AST_UNION_SPECIFIER || kind == C_AST_ENUM_SPECIFIER;
        bool prefix = word && (ast->data[item] == C_AST_WORD_CONST || ast->data[item] == C_AST_WORD_VOLATILE || ast->data[item] == C_AST_WORD_ATOMIC ||
                               ast->data[item] == C_AST_WORD_STATIC || ast->data[item] == C_AST_WORD_EXTERN);
        bool decoration = kind == C_AST_ATTRIBUTE_LIST || (word && ast->data[item] == C_AST_WORD_EXTENSION);
        result.is_typedef |= word && ast->data[item] == C_AST_WORD_TYPEDEF;
        result.is_constexpr |= word && ast->data[item] == C_AST_WORD_CONSTEXPR;
        if (aggregate)
        {
            other |= seen_aggregate;
            seen_aggregate = true;
        }
        else if (prefix)
        {
            other |= seen_aggregate;
        }
        else if (!decoration)
        {
            other = true;
        }
    }
    result.type_only = seen_aggregate && !other;
    return result;
}

// Follows a declarator's inner-declarator chain to its DECLARATOR_NAME. The
// chain reads in type-derivation order from the name outward, so the node
// whose inner declarator is the name is the first derivation: a function when
// the name is declared as `f(...)`, a pointer or array otherwise. A
// DECLARATOR_ATTRIBUTED decorates its inner declarator and derives nothing,
// so it is walked through without becoming the derivation.
BUSTER_GLOBAL_LOCAL u32 c_ast_corpus_declarator_name(CAst const* ast, u32 declarator, bool* first_derivation_is_function)
{
    u32 node = declarator;
    CAstKind derivation = C_AST_KIND_COUNT;
    bool done = false;
    while (!done)
    {
        CAstKind kind = (CAstKind)ast->kinds[node];
        u32 inner = C_AST_NODE_INVALID;
        if (kind == C_AST_DECLARATOR_POINTER && (ast->data[node] & 2u))
        {
            inner = c_ast_child_at(ast, node, c_ast_child_count(ast, node) - 1);
        }
        else if ((kind == C_AST_DECLARATOR_ARRAY || kind == C_AST_DECLARATOR_FUNCTION) && (ast->data[node] & 1u))
        {
            inner = c_ast_child_at(ast, node, 0);
        }
        else if (kind == C_AST_DECLARATOR_ATTRIBUTED)
        {
            inner = c_ast_child_at(ast, node, 1);
        }
        if (kind == C_AST_DECLARATOR_NAME)
        {
            done = true;
        }
        else if (inner == C_AST_NODE_INVALID)
        {
            node = C_AST_NODE_INVALID;
            done = true;
        }
        else
        {
            derivation = kind == C_AST_DECLARATOR_ATTRIBUTED ? derivation : kind;
            node = inner;
        }
    }
    *first_derivation_is_function = derivation == C_AST_DECLARATOR_FUNCTION;
    return node;
}

BUSTER_GLOBAL_LOCAL bool c_ast_corpus_is_declarator(CAstKind kind)
{
    return kind == C_AST_DECLARATOR_NAME || kind == C_AST_DECLARATOR_POINTER || kind == C_AST_DECLARATOR_ATTRIBUTED || kind == C_AST_DECLARATOR_ARRAY ||
           kind == C_AST_DECLARATOR_FUNCTION;
}

// Derives the records from the tree alone. With `records` null it only counts;
// otherwise it fills them and the caller sized the array from that count.
BUSTER_GLOBAL_LOCAL u32 c_ast_corpus_records(Arena* arena, CAst const* ast, CAstCorpusRecord* records)
{
    u32 count = 0;
    u32 external_count = c_ast_child_count(ast, ast->root);
    u32* externals = arena_allocate(arena, u32, external_count + 1);
    c_ast_children(ast, ast->root, externals, external_count);
    for (u32 group = 0; group < external_count; group += 1)
    {
        u32 external = externals[group];
        CAstKind kind = (CAstKind)ast->kinds[external];
        CAstCorpusRecord record = {
            .token_start = ast->tokens[external],
            .name_token = C_ID_UNDERLYING_INVALID,
            .group = group,
        };
        if (kind == C_AST_FUNCTION_DEFINITION)
        {
            u32 child_count = c_ast_child_count(ast, external);
            u32* children = arena_allocate(arena, u32, child_count);
            c_ast_children(ast, external, children, child_count);
            CAstCorpusSpecifiers specifiers = c_ast_corpus_specifiers(ast, children[0]);
            bool is_function = false;
            u32 name = c_ast_corpus_declarator_name(ast, children[1], &is_function);
            record.kind = C_PARSER_DECLARATION_FUNCTION;
            record.is_definition = true;
            record.is_typedef = specifiers.is_typedef;
            record.is_constexpr = specifiers.is_constexpr;
            record.name_token = name == C_AST_NODE_INVALID ? C_ID_UNDERLYING_INVALID : ast->tokens[name];
            record.body_start = ast->tokens[children[child_count - 1]] + 1;
            if (records)
            {
                records[count] = record;
            }
            count += 1;
        }
        else if (kind == C_AST_DECLARATION)
        {
            u32 child_count = c_ast_child_count(ast, external);
            u32* children = arena_allocate(arena, u32, child_count);
            c_ast_children(ast, external, children, child_count);
            CAstCorpusSpecifiers specifiers = c_ast_corpus_specifiers(ast, children[0]);
            record.is_typedef = specifiers.is_typedef;
            record.is_constexpr = specifiers.is_constexpr;
            if (child_count == 1)
            {
                record.kind = specifiers.is_typedef ? C_PARSER_DECLARATION_TYPEDEF
                              : specifiers.type_only ? C_PARSER_DECLARATION_TYPE
                                                     : C_PARSER_DECLARATION_UNKNOWN;
                // A declaration that names no declarator and no tag is classified
                // by c_parse_ast's name heuristic: OBJECT when any identifier
                // that is not a keyword appears in it (a typedef-name specifier,
                // or the name inside a C23 attribute list), UNKNOWN when none
                // does. The grammar has no such distinction, so either matches.
                record.kind_open = !specifiers.is_typedef && !specifiers.type_only;
                if (records)
                {
                    records[count] = record;
                }
                count += 1;
            }
            for (u32 child = 1; child < child_count; child += 1)
            {
                u32 parts[8];
                u32 part_count = c_ast_children(ast, children[child], parts, BUSTER_ARRAY_LENGTH(parts));
                u32 declarator = C_AST_NODE_INVALID;
                for (u32 part = 0; part < part_count && part < BUSTER_ARRAY_LENGTH(parts) && declarator == C_AST_NODE_INVALID; part += 1)
                {
                    declarator = c_ast_corpus_is_declarator((CAstKind)ast->kinds[parts[part]]) ? parts[part] : C_AST_NODE_INVALID;
                }
                bool is_function = false;
                u32 name = declarator == C_AST_NODE_INVALID ? C_AST_NODE_INVALID : c_ast_corpus_declarator_name(ast, declarator, &is_function);
                bool has_initializer = (ast->data[children[child]] & 4u) != 0;
                record.name_token = name == C_AST_NODE_INVALID ? C_ID_UNDERLYING_INVALID : ast->tokens[name];
                record.declarator_index = child - 1;
                record.kind = specifiers.is_typedef ? C_PARSER_DECLARATION_TYPEDEF
                              : is_function         ? C_PARSER_DECLARATION_FUNCTION
                              : name != C_AST_NODE_INVALID ? C_PARSER_DECLARATION_OBJECT
                                                           : C_PARSER_DECLARATION_UNKNOWN;
                record.is_definition = record.kind == C_PARSER_DECLARATION_OBJECT && has_initializer;
                if (records)
                {
                    records[count] = record;
                }
                count += 1;
            }
        }
        else if (kind == C_AST_STATIC_ASSERT || kind == C_AST_ASM_TOP_LEVEL || kind == C_AST_EMPTY_DECLARATION)
        {
            record.kind = kind == C_AST_STATIC_ASSERT   ? C_PARSER_DECLARATION_STATIC_ASSERT
                          : kind == C_AST_ASM_TOP_LEVEL ? C_PARSER_DECLARATION_ASSEMBLY
                                                        : C_PARSER_DECLARATION_UNKNOWN;
            if (records)
            {
                records[count] = record;
            }
            count += 1;
        }
    }
    return count;
}

// The first disagreement between the earlier syntax pass and the records, or
// an empty string.
BUSTER_GLOBAL_LOCAL String8 c_ast_corpus_compare(Arena* arena, CParserResult const* syntax, CAstCorpusRecord const* records, u32 record_count,
                                                 CAst const* ast)
{
    String8 mismatch = {0};
    if (syntax->declaration_count != record_count)
    {
        mismatch = string_format(arena, S8("declaration count: c_parse_ast {u32}, tree {u32}"), syntax->declaration_count, record_count);
    }
    u32 eof_token = ast->tokens[ast->root];
    u32 index = 0;
    for (CParserDeclaration const* old = syntax->first_declaration; old && index < record_count && !mismatch.length; old = old->next, index += 1)
    {
        CAstCorpusRecord const* record = &records[index];
        bool function = old->kind == C_PARSER_DECLARATION_FUNCTION;
        u32 old_name = function ? old->function_name_token : old->name_token;
        bool group_end = index + 1 == record_count || records[index + 1].group != record->group;
        u32 next_start = index + 1 == record_count ? eof_token : records[index + 1].token_start;
        String8 field = {0};
        u64 expected = 0;
        u64 actual = 0;
        bool kind_matches = record->kind_open ? (old->kind == C_PARSER_DECLARATION_OBJECT || old->kind == C_PARSER_DECLARATION_UNKNOWN)
                                              : old->kind == record->kind;
        if (!kind_matches)
        {
            field = S8("kind");
            expected = (u64)old->kind;
            actual = (u64)record->kind;
        }
        else if (old->is_definition != record->is_definition)
        {
            field = S8("is_definition");
            expected = old->is_definition;
            actual = record->is_definition;
        }
        else if (old->is_typedef != record->is_typedef)
        {
            field = S8("is_typedef");
            expected = old->is_typedef;
            actual = record->is_typedef;
        }
        else if (old->is_constexpr != record->is_constexpr)
        {
            field = S8("is_constexpr");
            expected = old->is_constexpr;
            actual = record->is_constexpr;
        }
        else if (old->token_start != record->token_start)
        {
            field = S8("token_start");
            expected = old->token_start;
            actual = record->token_start;
        }
        else if (record->name_token != C_ID_UNDERLYING_INVALID && old_name != record->name_token)
        {
            field = S8("name_token");
            expected = old_name;
            actual = record->name_token;
        }
        else if (old->body_start != record->body_start)
        {
            field = S8("body_start");
            expected = old->body_start;
            actual = record->body_start;
        }
        else if (old->is_declarator_continuation != (record->declarator_index > 0))
        {
            field = S8("is_declarator_continuation");
            expected = old->is_declarator_continuation;
            actual = record->declarator_index > 0;
        }
        else if (group_end && old->token_start + old->token_count != next_start)
        {
            field = S8("end of the external declaration");
            expected = (u64)old->token_start + old->token_count;
            actual = next_start;
        }
        if (field.length)
        {
            mismatch = string_format(arena, S8("record {u32} (token {u32}): {S8}: c_parse_ast {u64}, tree {u64}"), index, record->token_start, field, expected,
                                     actual);
        }
    }
    return mismatch;
}

// ---- the inputs ----

typedef struct CAstCorpusPin CAstCorpusPin;
struct CAstCorpusPin
{
    String8 path;
    String8 reason;
};

// Fixtures whose preprocessing succeeds and whose tree is incomplete. Each was
// classified against c_analyze_semantics_only once, offline: these are the
// only inputs where the tree refuses what the earlier pipeline accepts or
// where the input is not C. Every other tests/**/*.c file that preprocesses
// builds a complete tree, including the many fixtures that exist to be
// rejected by semantic analysis (invalid bit-field widths, labels, missing
// members, conflicting declarations): those are valid syntax.
BUSTER_GLOBAL_LOCAL CAstCorpusPin const c_ast_corpus_pins[] = {
    {S8_INITIALIZER("tests/basic_c_macro_options.c"), S8_INITIALIZER("a preprocessor-output fixture (`empty_begin EMPTY empty_end`), not C syntax")},
    {S8_INITIALIZER("tests/basic_c_sizeof_parenthesized_type.c"),
     S8_INITIALIZER("invalid by design: `sizeof ((T))` names a type where a value is required; semantic analysis rejects it too")},
    {S8_INITIALIZER("tests/differential/reject_syntax.c"),
     S8_INITIALIZER("invalid by design: `return (1 + );`; `ide cc -fsyntax-only` accepts it (the semantics-only pass does not lower bodies), `-c` rejects it")},
};

// Fixtures that need `-std=c23`, as tools/native_retirement_contract.py
// FIXTURE_RECIPES records; tests/basic_c_dialect.c is run in every dialect by
// c_ast_corpus_dialect_matrix instead.
BUSTER_GLOBAL_LOCAL String8 const c_ast_corpus_c23[] = {
    S8_INITIALIZER("tests/basic_c_constexpr.c"),
    S8_INITIALIZER("tests/basic_c_constexpr_leaf.c"),
    S8_INITIALIZER("tests/basic_c_nullptr.c"),
    S8_INITIALIZER("tests/basic_c_typeof.c"),
};

// The corpus can shrink only by being noticed: files that cannot preprocess
// (system headers, a foreign target's #error, a missing -D) are skipped, and
// these bounds fail the fixture if that population collapses.
enum
{
    C_AST_CORPUS_BUILT_FLOOR = 340,
    C_AST_CORPUS_SKIPPED_CEILING = 48,
    // Records compared against c_parse_ast, so a comparison that silently
    // sees nothing cannot pass.
    C_AST_CORPUS_RECORD_FLOOR = 6000,
    // Records the tree split (c_parse_ast_from_tree) published itself rather
    // than by falling back: about 6,100 from the fixtures and constructs on
    // Linux x86-64 (fewer fixtures preprocess on other hosts), and about
    // 15,900 in all with the frontend's own sources.
    C_AST_CORPUS_SPLIT_RECORD_FLOOR = 4800,
    C_AST_CORPUS_HOSTED_SPLIT_RECORD_FLOOR = 13000,
    // Tree expression-typer answers checked against the type machine
    // (c_ast_corpus_types): about 56,600 from the fixtures on Linux x86-64
    // (fewer with the fixtures preprocessed for Windows AArch64, which reach
    // fewer typed bodies: about 50,300 and 50,800 for each stage-3 change
    // alone), and about 338,900 in all with the frontend's own sources where
    // the host headers exist.
    C_AST_CORPUS_TYPE_ANSWER_FLOOR = 47000,
    C_AST_CORPUS_HOSTED_TYPE_ANSWER_FLOOR = 290000,
    // Designator probes the const-assignment walk skipped in the fixtures,
    // each checked against the machine: about 1,400 (1,370 for
    // aarch64-windows).
    C_AST_CORPUS_TYPE_PROBE_FLOOR = 1200,
    // Verified answers to members of const or volatile aggregates, whose row
    // is a shared qualified copy (c_parse_intern_qualified_type): 6 from the
    // fixtures on Linux x86-64, and 776 in all with the frontend's own
    // sources.
    C_AST_CORPUS_TYPE_SHARED_MEMBER_FLOOR = 4,
    C_AST_CORPUS_HOSTED_TYPE_SHARED_MEMBER_FLOOR = 620,
    // Statement-plan segments the body binder bound in one pass, and the
    // identifier uses they bound, in the analyses with the tree: about 6,800
    // segments and 25,700 uses from the fixtures on Linux x86-64, and about
    // 34,300 and 129,400 in all with the frontend's own sources.
    C_AST_CORPUS_STATEMENT_SEGMENT_FLOOR = 5400,
    C_AST_CORPUS_STATEMENT_USE_FLOOR = 20000,
    C_AST_CORPUS_HOSTED_STATEMENT_SEGMENT_FLOOR = 27000,
    C_AST_CORPUS_HOSTED_STATEMENT_USE_FLOOR = 100000,
    // Analysis model rows compared field by field with and without the tree
    // (c_ast_corpus_models_differ): about 125,700 from the fixtures on Linux
    // x86-64 and about 497,400 in all with the frontend's own sources.
    C_AST_CORPUS_MODEL_ROW_FLOOR = 100000,
    C_AST_CORPUS_HOSTED_MODEL_ROW_FLOOR = 440000,
    // Block declarators the split took from their plan hints, and the plain
    // initializers among them: about 3,550 and 2,960 from the fixtures on
    // Linux x86-64, and about 19,000 and 18,100 in all with the frontend's
    // own sources. No hint may be refused.
    C_AST_CORPUS_STATEMENT_DECLARATOR_FLOOR = 2800,
    C_AST_CORPUS_STATEMENT_INITIALIZER_FLOOR = 2300,
    C_AST_CORPUS_HOSTED_STATEMENT_DECLARATOR_FLOOR = 15000,
    C_AST_CORPUS_HOSTED_STATEMENT_INITIALIZER_FLOOR = 14000,
    // Declaration specifier runs read from their DECL_SPECIFIERS nodes in the
    // analyses with the tree, each analysis's model compared field by field
    // with the machine's: about 19,800 from the fixtures on Linux x86-64, and
    // about 60,000 in all with the frontend's own sources.
    C_AST_CORPUS_SPECIFIER_ANSWER_FLOOR = 15000,
    C_AST_CORPUS_HOSTED_SPECIFIER_ANSWER_FLOOR = 50000,
    // Statements planned around their initializer lists' braces (compound
    // literals in an expression, a header, a return or a do-while tail): about
    // 460 from the fixtures on Linux x86-64, and about 4,230 in all with the
    // frontend's own sources.
    C_AST_CORPUS_STATEMENT_BRACED_FLOOR = 380,
    C_AST_CORPUS_HOSTED_STATEMENT_BRACED_FLOOR = 3500,
    // Type-name runs skipped as no type name, each checked against the
    // machine in verify mode, and type-name runs read from the tree, in the
    // same analyses: about 1,250 skipped and 2,270 read from the fixtures on
    // Linux x86-64, and about 18,300 and 13,300 in all with the frontend's
    // own sources.
    C_AST_CORPUS_TYPE_NAME_SKIP_FLOOR = 900,
    C_AST_CORPUS_TYPE_NAME_ANSWER_FLOOR = 1600,
    C_AST_CORPUS_HOSTED_TYPE_NAME_SKIP_FLOOR = 15000,
    C_AST_CORPUS_HOSTED_TYPE_NAME_ANSWER_FLOOR = 11000,
    // Member specifier runs read from the tree inside tag definitions, each
    // compared with the machine's scalar frame in verify mode: about 1,790
    // from the fixtures on Linux x86-64, and about 11,150 in all with the
    // frontend's own sources.
    C_AST_CORPUS_MEMBER_ANSWER_FLOOR = 1400,
    C_AST_CORPUS_HOSTED_MEMBER_ANSWER_FLOOR = 9000,
};

BUSTER_GLOBAL_LOCAL bool c_ast_corpus_in(String8 const* paths, u32 count, String8 path)
{
    bool found = false;
    for (u32 index = 0; index < count && !found; index += 1)
    {
        found = string_equal(path, paths[index]);
    }
    return found;
}

BUSTER_GLOBAL_LOCAL bool c_ast_corpus_pinned(String8 path)
{
    bool pinned = false;
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(c_ast_corpus_pins) && !pinned; index += 1)
    {
        pinned = string_equal(path, c_ast_corpus_pins[index].path);
    }
    return pinned;
}

typedef struct CAstCorpusTally CAstCorpusTally;
struct CAstCorpusTally
{
    u64 files;
    u64 skipped;
    u64 pinned;
    u64 built;
    u64 records;
    u64 nodes;
    u64 tokens;
    // The tree expression typer over the same inputs (c_ast_corpus_types).
    u64 type_answers;
    u64 type_compared;
    // Designator probes the const-assignment walk skipped and verify mode
    // checked against the machine.
    u64 type_probes;
    // Verified answers to member queries whose row is a shared qualified copy.
    u64 type_shared_members;
    // The declaration split from the tree (c_parse_ast_from_tree) over the
    // same inputs: units it published, records and body assertion ranges it
    // published, units it handed to the token walker and why, and every
    // record compared field by field with c_parse_ast's.
    u64 split_units;
    u64 split_records;
    u64 split_assertions;
    u64 split_fallbacks;
    u64 split_fallback_counts[C_PARSER_TREE_FALLBACK_COUNT];
    u64 split_compared;
    // The body binder's statement plans over the same inputs (the analysis
    // with the tree in c_ast_corpus_types): bodies planned, segments and the
    // tokens and uses they bound, hints taken, declarator hints taken (with
    // their plain initializers and refusals), bodies left to the token walk,
    // and identifier uses compared field by field with the walk's own.
    u64 statement_bodies;
    u64 statement_segments;
    u64 statement_segment_tokens;
    u64 statement_uses;
    u64 statement_hints;
    u64 statement_declarators;
    u64 statement_initializers;
    u64 statement_declarator_declines;
    u64 statement_braced;
    u64 statement_fallbacks;
    u64 statement_compared;
    // Analysis model rows compared field by field in the same analyses
    // (c_ast_corpus_models_differ).
    u64 model_rows;
    // The declaration specifier read over the same inputs (the analysis with
    // the tree in c_ast_corpus_types): runs read from DECL_SPECIFIERS nodes,
    // runs left to the type machine, and type rows compared field by field.
    u64 specifier_answers;
    u64 specifier_fallbacks;
    u64 specifier_compared;
    // The type-name runs over the same inputs: runs skipped as no type name,
    // and checked against the machine in verify mode; runs read from the tree.
    u64 type_name_skips;
    u64 type_name_checked;
    u64 type_name_answers;
    // The member specifier runs over the same inputs: runs read from the
    // tree, and those of them compared with the machine in verify mode.
    u64 member_answers;
    u64 member_compared;
    // The retained facts (CAstFacts, GitHub #4062) of the analysis with the
    // tree: fact candidates, those left without a type, and published rows
    // found past the type table.
    u64 fact_candidates;
    u64 fact_missing;
    u64 fact_stale;
    u64 fact_unaccounted;
};

// c_parse_ast_from_tree on one input whose tree is complete, held to
// c_parse_ast's result, with the outcome added to the tally.
BUSTER_GLOBAL_LOCAL UnitTestResult c_ast_corpus_split(UnitTestArguments* arguments, Arena* arena, String8 label, CPreprocessResult preprocess,
                                                      CAst const* ast, CParserResult const* syntax, CAstCorpusTally* tally)
{
    UnitTestResult result = {0};
    CParserTreeStatistics statistics = {0};
    CParserResult derived = c_parse_ast_from_tree(arena, preprocess, ast, &statistics);
    String8 difference = c_ast_corpus_split_differ(arena, syntax, &derived);
    BUSTER_TEST_RAW(arguments, difference.length == 0, string_format(arena, S8("{S8}: tree split: {S8}"), label, difference));
    tally->split_units += statistics.units;
    tally->split_records += statistics.records;
    tally->split_assertions += statistics.assertions;
    tally->split_fallbacks += statistics.fallbacks;
    for (u32 reason = 0; reason < C_PARSER_TREE_FALLBACK_COUNT; reason += 1)
    {
        tally->split_fallback_counts[reason] += statistics.fallback_counts[reason];
    }
    tally->split_compared += derived.declaration_count;
    return result;
}

// The tree expression typer (c_ast_types.c, #3102) on one input whose tree is
// complete and whose declarations c_parse_ast accepted. Semantic analysis runs
// three times on fresh declaration splits: without the tree, with it, and with
// it in verify mode, where the type machine also answers every query the tree
// answered. The tree may not change a diagnostic or the size of a type table,
// and the machine must agree with every tree answer. Every run interns rows
// (CParserResult.type_interning), as the pilot does, so the tables compared
// are the interned ones.
BUSTER_GLOBAL_LOCAL UnitTestResult c_ast_corpus_types(UnitTestArguments* arguments, String8 label, CPreprocessResult preprocess, CAst const* ast,
                                                      CAstCorpusTally* tally)
{
    UnitTestResult result = {0};
    // Analysis of a frontend source with its headers outgrows a test scratch
    // arena, so each pair of analyses gets an arena of its own.
    ArenaCreation creation = {.reserved_size = BUSTER_GB(16), .flags = {.no_pool = true}};
    Arena* arena = arena_create(creation);
    if (BUSTER_REQUIRE(arguments, arena != 0))
    {
        CParserResult plain_syntax = c_parse_ast_reference(arena, preprocess);
        plain_syntax.type_interning = true;
        CAnalysisResult plain = c_analyze_semantics_only(arena, preprocess, plain_syntax);
        CAstTypeStatistics statistics = {0};
        CAstStatementStatistics statements = {0};
        CAstSpecifierStatistics specifiers = {0};
        CParserResult typed_syntax = c_parse_ast_reference(arena, preprocess);
        typed_syntax.ast = ast;
        typed_syntax.ast_type_statistics = &statistics;
        typed_syntax.ast_statement_statistics = &statements;
        typed_syntax.ast_specifier_statistics = &specifiers;
        typed_syntax.type_interning = true;
        // The analysis with the tree also publishes the retained facts, which
        // must leave the model as the machine alone builds it (below).
        CAstFacts facts = {0};
        typed_syntax.ast_facts = c_ast_facts_create(&facts, ast) ? &facts : 0;
        CAnalysisResult typed = c_analyze_semantics_only(arena, preprocess, typed_syntax);
        BUSTER_TEST_RAW(arguments, c_ast_test_declaration_owners_consistent(&typed, ast),
                        string_format(arguments->arena, S8("{S8}: retained declaration owners"), label));
        CAstFactStatistics fact_counts = facts.statistics;
        c_ast_facts_release(&facts);
        u64 fact_gaps = 0;
        for (u32 gap = 0; gap < C_AST_FACT_GAP_COUNT; gap += 1)
        {
            fact_gaps += fact_counts.gaps[gap];
        }
        tally->fact_candidates += fact_counts.candidates;
        tally->fact_missing += fact_gaps;
        tally->fact_stale += fact_counts.stale;
        tally->fact_unaccounted += fact_counts.tree + fact_counts.machine + fact_counts.completed + fact_gaps != fact_counts.candidates;
        String8 difference = c_ast_corpus_analyses_differ(arguments->arena, &plain, &typed);
        BUSTER_TEST_RAW(arguments, difference.length == 0, string_format(arguments->arena, S8("{S8}: {S8}"), label, difference));
        // The body binder followed the tree's statement plans in the second
        // analysis; its bindings must be the token walk's, field by field.
        difference = c_ast_corpus_bindings_differ(arguments->arena, &plain, &typed);
        if (!difference.length)
        {
            difference = c_ast_corpus_models_differ(arguments->arena, &plain, &typed, &tally->model_rows);
        }
        BUSTER_TEST_RAW(arguments, difference.length == 0, string_format(arguments->arena, S8("{S8}: statement plan: {S8}"), label, difference));
        // It also read the declaration specifier runs it could from their
        // DECL_SPECIFIERS nodes; the model must be the machine's, field by field.
        difference = c_ast_corpus_model_differ(arguments->arena, &plain, &typed);
        BUSTER_TEST_RAW(arguments, difference.length == 0, string_format(arguments->arena, S8("{S8}: specifier read: {S8}"), label, difference));
        tally->specifier_answers += specifiers.primitive_answers + specifiers.typedef_answers;
        tally->specifier_fallbacks += specifiers.fallbacks;
        tally->specifier_compared += typed.type_count;
        tally->type_name_skips += specifiers.type_name_skips;
        tally->type_name_answers += specifiers.type_name_primitive_answers + specifiers.type_name_typedef_answers;
        tally->member_answers += specifiers.member_primitive_answers + specifiers.member_typedef_answers;
        tally->statement_bodies += statements.bodies;
        tally->statement_segments += statements.segments;
        tally->statement_segment_tokens += statements.segment_tokens;
        tally->statement_uses += statements.uses;
        tally->statement_hints += statements.declaration_hints + statements.loop_hints;
        tally->statement_declarators += statements.declarator_hints;
        tally->statement_initializers += statements.initializer_hints;
        tally->statement_declarator_declines += statements.declarator_declines;
        tally->statement_braced += statements.braced;
        tally->statement_fallbacks += statements.fallbacks;
        tally->statement_compared += typed.identifier_use_count;
        arena_destroy(arena, 1);
        arena = arena_create(creation);
        if (BUSTER_REQUIRE(arguments, arena != 0))
        {
            CParserResult verified_syntax = c_parse_ast_reference(arena, preprocess);
            verified_syntax.ast = ast;
            verified_syntax.type_interning = true;
            c_test_ast_type_verify_take();
            c_test_ast_type_names_verify_take();
            c_test_ast_members_verify_take();
            c_test_ast_type_verify_set(true);
            c_test_ast_type_names_verify_set(true);
            c_test_ast_members_verify_set(true);
            c_analyze_semantics_only(arena, preprocess, verified_syntax);
            c_test_ast_type_verify_set(false);
            c_test_ast_type_names_verify_set(false);
            c_test_ast_members_verify_set(false);
            CTestAstTypeVerify verify = c_test_ast_type_verify_take();
            // Every member run the tree read was also read by the machine's
            // scalar frame, rolled back, and must match it.
            CTestAstMemberVerify members = c_test_ast_members_verify_take();
            BUSTER_TEST_RAW(arguments, members.compared == specifiers.member_primitive_answers + specifiers.member_typedef_answers && members.mismatches == 0,
                            string_format(arguments->arena, S8("{S8}: {u64} member runs read, {u64} compared with the machine, {u64} differ"), label,
                                          specifiers.member_primitive_answers + specifiers.member_typedef_answers, members.compared, members.mismatches));
            tally->member_compared += members.compared;
            // Every type-name run the tree skipped as no type name ran through
            // the machine's attempt too, which must have failed silently.
            CTestAstTypeNameVerify names = c_test_ast_type_names_verify_take();
            BUSTER_TEST_RAW(arguments, names.checked == specifiers.type_name_skips,
                            string_format(arguments->arena, S8("{S8}: {u64} type names skipped, {u64} verified"), label, specifiers.type_name_skips,
                                          names.checked));
            BUSTER_TEST_RAW(arguments, names.answered == 0 && names.appended == 0 && names.diagnosed == 0,
                            string_format(arguments->arena,
                                          S8("{S8}: of {u64} type-name runs skipped, the machine answered {u64}, kept rows for {u64} and diagnosed {u64}"),
                                          label, names.checked, names.answered, names.appended, names.diagnosed));
            tally->type_name_checked += names.checked;
            BUSTER_TEST_RAW(arguments, verify.compared == statistics.answers,
                            string_format(arguments->arena, S8("{S8}: {u64} tree answers, {u64} verified"), label, statistics.answers, verify.compared));
            for (u32 index = 0; index < verify.first_count; index += 1)
            {
                CTestAstTypeMismatch const* mismatch = verify.first + index;
                String8 first = mismatch->start < preprocess.token_count ? c_token_spelling(preprocess.spelling_base, preprocess.tokens[mismatch->start])
                                                                         : S8("?");
                BUSTER_TEST_RAW(arguments, false,
                                string_format(arguments->arena,
                                              S8("{S8}: tree answer for tokens [{u32}, {u32}) at '{S8}' ({S8}) disagrees with the machine: reasons {u32}, "
                                                 "tree kind {u32}, machine kind {u32}, machine valid {u32}, checked {u32}"),
                                              label, mismatch->start, mismatch->end, first, c_ast_kind_name((CAstKind)mismatch->node_kind),
                                              mismatch->reasons, mismatch->tree_type_kind, mismatch->machine_type_kind, (u32)mismatch->machine_valid,
                                              (u32)mismatch->checked));
            }
            BUSTER_TEST_RAW(arguments, verify.mismatches == verify.first_count,
                            string_format(arguments->arena, S8("{S8}: {u64} tree answers disagree with the machine"), label, verify.mismatches));
            BUSTER_TEST_RAW(arguments, verify.probe_mismatches == 0,
                            string_format(arguments->arena, S8("{S8}: the machine typed {u64} of {u64} skipped designator probes, or changed the model"),
                                          label, verify.probe_mismatches, verify.probes));
            tally->type_answers += statistics.answers;
            tally->type_compared += verify.compared;
            tally->type_probes += verify.probes;
            tally->type_shared_members += verify.shared_members;
            arena_destroy(arena, 1);
        }
    }
    return result;
}

// One input: preprocess, run the earlier syntax pass and the tree builder, and
// hold the tree to the claims above. `label` names the input in failures (a
// path, or the source text of a construct); `required` makes a preprocessing
// error a failure instead of a skip.
BUSTER_GLOBAL_LOCAL UnitTestResult c_ast_corpus_unit(UnitTestArguments* arguments, String8 label, String8 source, CPreprocessOptions options, bool required,
                                                     CAstCorpusTally* tally)
{
    UnitTestResult result = {0};
    TemporalArena temporary = scratch_begin(&arguments->arena, 1);
    tally->files += 1;
    options.target = target_native;
    options.data_layout = target_data_layout(target_native);
    CPreprocessResult preprocess = c_preprocess(temporary.arena, source, options);
    if (preprocess.error_count)
    {
        tally->skipped += 1;
        if (required)
        {
            String8 message = preprocess.diagnostic_count ? preprocess.diagnostics[0].message : S8("?");
            BUSTER_TEST_RAW(arguments, false, string_format(temporary.arena, S8("{S8}: preprocessing failed: {S8}"), label, message));
        }
    }
    else
    {
        CParserResult syntax = c_parse_ast_reference(temporary.arena, preprocess);
        CAstResult built = c_ast_build(temporary.arena, preprocess, (CAstOptions){0});
        bool pinned = c_ast_corpus_pinned(label);
        BUSTER_TEST_RAW(arguments, built.complete != pinned,
                        string_format(temporary.arena, S8("{S8}: {S8}{S8}"), label, pinned ? S8("pinned but the tree builds") : S8("tree rejected: "),
                                      built.diagnostic_count && !pinned ? built.diagnostics[0].message : S8("")));
        if (built.complete)
        {
            BUSTER_TEST_RAW(arguments, c_ast_validate(&built.ast) == C_AST_NODE_INVALID, label);
            u32* matches = c_ast_test_matches(temporary.arena, preprocess);
            CAstSpans spans = {0};
            if (matches && c_ast_spans_build(temporary.arena, &built.ast, preprocess.tokens, (u32)preprocess.token_count, matches, &spans))
            {
                BUSTER_TEST_RAW(arguments, c_ast_test_spans_consistent(&built.ast, &spans, preprocess, matches),
                                string_format(temporary.arena, S8("{S8}: token spans"), label));
            }
            c_ast_test_merge(&result, c_ast_corpus_split(arguments, temporary.arena, label, preprocess, &built.ast, &syntax, tally));
            if (!syntax.diagnostic_count)
            {
                u32 record_count = c_ast_corpus_records(temporary.arena, &built.ast, 0);
                CAstCorpusRecord* records = arena_allocate(temporary.arena, CAstCorpusRecord, record_count + 1);
                c_ast_corpus_records(temporary.arena, &built.ast, records);
                String8 mismatch = c_ast_corpus_compare(temporary.arena, &syntax, records, record_count, &built.ast);
                BUSTER_TEST_RAW(arguments, mismatch.length == 0, string_format(temporary.arena, S8("{S8}: {S8}"), label, mismatch));
                tally->records += record_count;
                c_ast_test_merge(&result, c_ast_corpus_types(arguments, label, preprocess, &built.ast, tally));
            }
            tally->built += 1;
            tally->nodes += built.ast.node_count;
        }
        tally->pinned += pinned && !built.complete;
        tally->tokens += preprocess.token_count;
        c_ast_release(&built.ast);
    }
    c_test_scratch_end(temporary);
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult c_ast_corpus_file(UnitTestArguments* arguments, String8 path, CPreprocessOptions options, bool required,
                                                     CAstCorpusTally* tally)
{
    UnitTestResult result = {0};
    TemporalArena temporary = scratch_begin(&arguments->arena, 1);
    ByteSlice file = file_read(temporary.arena, path, (FileReadOptions){0});
    if (BUSTER_REQUIRE(arguments, file.pointer && file.length))
    {
        String8 source = {.pointer = (char8*)file.pointer, .length = file.length};
        options.source_path = path;
        c_ast_test_merge(&result, c_ast_corpus_unit(arguments, path, source, options, required, tally));
    }
    c_test_scratch_end(temporary);
    return result;
}

// Declaration shapes the corpus holds few or none of: lists that mix object,
// function and function-pointer declarators, typedef lists, parenthesized and
// pointer-returning function declarators, K&R definitions, stray semicolons,
// type-only declarations, attributes and asm labels around declarators, and
// the C23 forms. Each runs through the same differential as a file.
typedef struct CAstCorpusConstruct CAstCorpusConstruct;
struct CAstCorpusConstruct
{
    String8 source;
    CPreprocessDialect dialect;
};

BUSTER_GLOBAL_LOCAL CAstCorpusConstruct const c_ast_corpus_constructs[] = {
    {S8_INITIALIZER("int a, b = 2, *c, d[3], (*e)(int), f(int), g = 1;"), C_PREPROCESS_DIALECT_GNU17},
    {S8_INITIALIZER("typedef int T, *PT, F(int), (*PF)(int), A[4]; T t; PT pt; F fn; PF pf, pf2 = 0;"), C_PREPROCESS_DIALECT_GNU17},
    {S8_INITIALIZER("int (f)(int); int (*g(int))(int); int *h(void); int (*pa(void))[3]; int (*tbl[2])(int); int (x), (y), (*z);"), C_PREPROCESS_DIALECT_GNU17},
    {S8_INITIALIZER("int k(a, b) int a; char b; { return a + b; } int k2(a) int a; { return a; } int k3(); int k4(void) { return 0; }"), C_PREPROCESS_DIALECT_GNU17},
    {S8_INITIALIZER("int x; ; void f(void) {} ; ; int y;;"), C_PREPROCESS_DIALECT_GNU17},
    {S8_INITIALIZER("__asm__(\"nop\"); __asm__(\"\" ); __asm(\"x\"); int after;"), C_PREPROCESS_DIALECT_GNU17},
    {S8_INITIALIZER("_Static_assert(1, \"m\"); _Static_assert(sizeof(int) == 4); int v;"), C_PREPROCESS_DIALECT_GNU23},
    {S8_INITIALIZER("static_assert(1, \"m\"); static_assert(sizeof(int) == 4); int v;"), C_PREPROCESS_DIALECT_C23},
    {S8_INITIALIZER("struct S { int a; }; union U; enum E { A }; const struct S2; static struct S3 { int a; }; extern union U4 { int a; };"), C_PREPROCESS_DIALECT_GNU17},
    {S8_INITIALIZER("struct S { int a; } s, *p; enum E { A } e = A; struct S2 { int a; } const c2 = {1}; inline struct S3 { int a; };"), C_PREPROCESS_DIALECT_GNU17},
    {S8_INITIALIZER("enum E : unsigned char { B } e2; enum F : int { C }; enum G : long { D };"), C_PREPROCESS_DIALECT_C23},
    {S8_INITIALIZER("int x __asm__(\"y\"), z; int f1(void) __asm__(\"g\"), f2(void); int w __asm__(\"w\") = 1;"), C_PREPROCESS_DIALECT_GNU17},
    {S8_INITIALIZER("int f(void) __attribute__((noreturn)), g(void); int __attribute__((aligned(4))) aa, bb __attribute__((unused)); __attribute__((unused)) int cc;"), C_PREPROCESS_DIALECT_GNU17},
    {S8_INITIALIZER("int a, __attribute__((unused)) b; struct __attribute__((packed)) P { char c; }; struct __attribute__((packed)) P2 { char c; } p2;"), C_PREPROCESS_DIALECT_GNU17},
    {S8_INITIALIZER("void v(int, ...); int (*fp)(int, ...) = 0; int (*gp(int, ...))(char); typedef void (*cb)(int, ...);"), C_PREPROCESS_DIALECT_GNU17},
    {S8_INITIALIZER("static inline int sf(void) { return 0; } extern inline int ef(void); _Noreturn void nr(void) { for (;;) { } }"), C_PREPROCESS_DIALECT_GNU17},
    {S8_INITIALIZER("int arr[sizeof(int[3])] = {1}, *pp; int m[2][3] = {{1}, {2}}; char s[] = \"a\" \"b\";"), C_PREPROCESS_DIALECT_GNU17},
    {S8_INITIALIZER("typedef struct { int a; } TS; TS ts, *pts; TS; typedef struct SS { int a; } SSD;"), C_PREPROCESS_DIALECT_GNU17},
    {S8_INITIALIZER("int; long; typedef int; typedef struct S0 { int a; };"), C_PREPROCESS_DIALECT_GNU17},
    {S8_INITIALIZER("_Alignas(8) int al; _Thread_local int tl; register int rg; static const volatile int cv = 1; _Atomic int at; _Atomic(int) at2;"), C_PREPROCESS_DIALECT_GNU17},
    {S8_INITIALIZER("int g3(void) { struct L { int a; } l; { int x = ({ 1; }); (void)x; } switch (0) { case 1: break; } return l.a; } int after3;"), C_PREPROCESS_DIALECT_GNU17},
    {S8_INITIALIZER("[[maybe_unused]] int attr1; int attr2 [[maybe_unused]]; [[nodiscard]] int attr3(void); [[maybe_unused]]; [[deprecated]] struct SA { int a; };"), C_PREPROCESS_DIALECT_C23},
    {S8_INITIALIZER("typeof(int) tx; __typeof__(tx) ty, *tz; typeof_unqual(tx) tu; __auto_type au = 1;"), C_PREPROCESS_DIALECT_GNU23},
    // #3493: typeof's comma operand at file scope and in a body.
    {S8_INITIALIZER("int *p; __typeof__(0, p) q; int f(int a, int b, int c) { __typeof__(a ? b : c, p) r = p; return *r + *q; }"), C_PREPROCESS_DIALECT_GNU17},
    {S8_INITIALIZER("int *p; typeof_unqual(1, p) u; int g(void) { typeof(0, p) v = u; return *v; }"), C_PREPROCESS_DIALECT_C23},
    {S8_INITIALIZER("constexpr int ce = 1; constexpr int cf = 2, cg = 3; static constexpr double cd = 1.5; auto ai = 1; bool bb = true;"), C_PREPROCESS_DIALECT_C23},
    {S8_INITIALIZER("__extension__ int ex; __extension__ struct SX { int a; }; __extension__ typedef int ET; __extension__ int exf(void) { return 0; }"), C_PREPROCESS_DIALECT_GNU17},
    {S8_INITIALIZER("int f(int a, int b) { return a ? b : -b; } int (*pf)(int, int) = f, (*pg)(int, int) = f; int af[] = {1, 2, 3}, ag = 4;"), C_PREPROCESS_DIALECT_GNU17},
    {S8_INITIALIZER("int bf(void) { int (*p)(void) = 0; return p ? 1 : 0; } int ca(int n, int a[n]); int cb(int n, int a[static n]); int cc(int a[*]);"), C_PREPROCESS_DIALECT_GNU17},
    {S8_INITIALIZER("struct { int a; } anon_obj, anon_arr[2]; union { int a; float b; } anon_u = {1}; enum { ANON } anon_e;"), C_PREPROCESS_DIALECT_GNU17},
    {S8_INITIALIZER("int c0 = (int){1}, c1 = sizeof(struct { int a; }), c2 = sizeof(int[2]) + _Alignof(int);"), C_PREPROCESS_DIALECT_GNU17},
    {S8_INITIALIZER("int (*signal(int, void (*)(int)))(int); void (*signal2(int sig, void (*func)(int)))(int) { return func; } int (*getfn(void))(void) { return 0; }"), C_PREPROCESS_DIALECT_GNU17},
    {S8_INITIALIZER("int *(*fp1)(void), (*fp2)(void); static int (*const table[])(void) = {0, 0}; unsigned long long (*fn(void))(void); int (*(*pp)(void))[3];"), C_PREPROCESS_DIALECT_GNU17},
    {S8_INITIALIZER("typedef int T; int f1(T); int g1(T x); T (*f2(void))(T); void f3(T (*cb)(T)) { } int f4(T) ; typedef void (*sighandler_t)(int); sighandler_t sig3(int, sighandler_t);"), C_PREPROCESS_DIALECT_GNU17},
    {S8_INITIALIZER("const char *names[] = {\"a\", \"b\"}, *last; int *cl = (int[]){1, 2}, q; struct S { int a; } s1 = {.a = 1}, s2 = {.a = 2}; int arr3[3] = {[1] = 5};"), C_PREPROCESS_DIALECT_GNU17},
    {S8_INITIALIZER("int z __attribute__((section(\".x\"))) = 1; long double ld; unsigned long ul; int a2 = 1, b2[] = {1, 2}, c2(void); char *const *volatile ppcv; int *restrict rp;"), C_PREPROCESS_DIALECT_GNU17},
    {S8_INITIALIZER("void vf(void g(void)) { } int printf2(const char *fmt, ...) { return 0; } void h(int n, int (*p)[n]) { } int r1(int x[restrict]); void r2(int a[const 3]);"), C_PREPROCESS_DIALECT_GNU17},
    {S8_INITIALIZER("_Bool b = 1; _Complex double cd; __int128 big; typeof(int (*)(void)) fpt; void (*handlers[3])(int) = {0}, (*h2)(int); int defined; int inline_;"), C_PREPROCESS_DIALECT_GNU23},
    {S8_INITIALIZER("unsigned sz = sizeof(int(void)) + sizeof(int(*)(void)); char buf[sizeof(struct { int (*f)(void); })]; enum { EA = sizeof(int(*)(void)) } ee;"), C_PREPROCESS_DIALECT_GNU17},
    {S8_INITIALIZER("struct S { int (*cb)(void); } objs[] = {{0}}, one; union { void (*f)(int); } uu, *up; struct T { int a; } (*tf(void))[2]; struct T tt(void) { struct T r; return r; }"), C_PREPROCESS_DIALECT_GNU17},
    {S8_INITIALIZER("int main(int argc, char **argv) { (void)argv; return argc; } static void unused(void) { } int tail = 3;"), C_PREPROCESS_DIALECT_GNU17},
    {S8_INITIALIZER("typedef struct node { struct node *next; int (*visit)(struct node *); } node_t; node_t *head, **tailp = &head; static node_t sentinel = {0, 0};"), C_PREPROCESS_DIALECT_GNU17},
    {S8_INITIALIZER("__attribute__((constructor)) static void ctor(void) { } static void dtor(void) __attribute__((destructor)); __attribute__((noreturn)) void die(void); void die(void) { for (;;) { } }"), C_PREPROCESS_DIALECT_GNU17},
    {S8_INITIALIZER("int __attribute__((visibility(\"default\"))) pub(void) { return 0; } extern int __attribute__((weak)) wk; static __attribute__((always_inline)) inline int ai(void) { return 1; }"), C_PREPROCESS_DIALECT_GNU17},
    {S8_INITIALIZER("int e1 = 1 ? 2 : 3, e2 = (1, 2), e3 = sizeof(int) << 2; int e4[2 + 3], e5[] = {sizeof(e4) / sizeof(e4[0])}; float fl = 1.0f, *fp = &fl;"), C_PREPROCESS_DIALECT_GNU17},
    {S8_INITIALIZER("int * __attribute__((x)) pp2; int (* __attribute__((x)) pq)(void); int __attribute__((x)) *__attribute__((y)) p3;"), C_PREPROCESS_DIALECT_GNU17},
    {S8_INITIALIZER("void pf(int __attribute__((unused)) a, int b __attribute__((unused))); struct S4 { int a __attribute__((x)), b; } s4; int a4[2] __attribute__((x));"), C_PREPROCESS_DIALECT_GNU17},
    {S8_INITIALIZER("__attribute__((x)) int (*pfx)(void) __attribute__((y)) = 0; typedef int T5 __attribute__((aligned(4))); enum E5 { A5 __attribute__((deprecated)) = 1 } e5; int x6 __asm__(\"x\") __attribute__((unused));"), C_PREPROCESS_DIALECT_GNU17},
    {S8_INITIALIZER("int c, __attribute__((x)) d, e __attribute__((y));"), C_PREPROCESS_DIALECT_GNU17},
    {S8_INITIALIZER("_Static_assert(1, \"a\"); int s1; _Static_assert(1, \"b\"); int s2;"), C_PREPROCESS_DIALECT_GNU17},
    // #3142: plain file-scope `asm`, attributes opening a parenthesized
    // pointer declarator or preceding one, and a C23 opaque enum.
    {S8_INITIALIZER("asm(\"nop\"); int after;"), C_PREPROCESS_DIALECT_GNU17},
    {S8_INITIALIZER("int __attribute__((unused)) (*pfa)(void); int (__attribute__((unused)) *pb);"), C_PREPROCESS_DIALECT_GNU17},
    {S8_INITIALIZER("enum G : long;"), C_PREPROCESS_DIALECT_C23},
    // Both lists on one later declarator (INIT_DECLARATOR's leading and trailing
    // slots), and an attribute list opening a parenthesized declarator that is
    // not a pointer (DECLARATOR_ATTRIBUTED).
    {S8_INITIALIZER("int a, __attribute__((x)) b __attribute__((y)); int c, __attribute__((x)) d __attribute__((y)) = 1, __attribute__((z)) *e;"), C_PREPROCESS_DIALECT_GNU17},
    {S8_INITIALIZER("int (__attribute__((unused)) pa); int (__attribute__((x)) pf)(void); int (__attribute__((x)) pr)[3]; int (__attribute__((x)) (pq))(int);"), C_PREPROCESS_DIALECT_GNU17},
    {S8_INITIALIZER("int (__attribute__((x)) fd)(void) { return 0; } int pt, (__attribute__((y)) pu) = 1;"), C_PREPROCESS_DIALECT_GNU17},
    // #3215: declarators in redundant groups, which the walker walks out of
    // to the derivation nearest the name: objects, functions, a definition
    // whose list sits inside the group, groups in a list and after a typedef
    // name, and pointers inside and outside the groups.
    {S8_INITIALIZER("int f(int x) { return x + 1; } int ((*pq))(int) = f; int (gd(int a)) { return a; } int zz;"), C_PREPROCESS_DIALECT_GNU17},
    {S8_INITIALIZER("int (x); int ((x2))[3]; void ((h))(void) {} void ((h2))(void); int ((x3)) = 1; int (((*p3)))(int); int (*(f3))(int);"),
     C_PREPROCESS_DIALECT_GNU17},
    {S8_INITIALIZER("int a, ((b)), (c)[2], ((*d))(void), (e(void)), (((*g))); int (gd2(int a)), zz2; int (*(g3(int a)))(void) { return 0; }"),
     C_PREPROCESS_DIALECT_GNU17},
    {S8_INITIALIZER("typedef int T; T ((t))[2]; T ((*tp))(T); T (*(tq))(int); void pf(int ((a))[3], int ((*cb))(int)) { } void pg(T (a)) { }"),
     C_PREPROCESS_DIALECT_GNU17},
    // #3368: old-style definitions whose name sits in a redundant group, with
    // and without a declaration list, and lists in groups in a declaration.
    {S8_INITIALIZER("int (k(a)) int a; { return a; } int (*(m(b)))(void) int b; { return 0; } int ((k4)(c, d)) int c; char *d; { return c; }"),
     C_PREPROCESS_DIALECT_GNU17},
    {S8_INITIALIZER("typedef int T; int (k3(T)) { return 0; } int (k5)(T) { return 0; } int x, (kd(int c)), (k2)(T);"), C_PREPROCESS_DIALECT_GNU17},
    {S8_INITIALIZER("typedef int (__attribute__((x)) TF)(int); TF tf; typedef int (__attribute__((x)) TA)[2]; TA ta; void (*(__attribute__((x)) hf))(void);"), C_PREPROCESS_DIALECT_GNU17},
    {S8_INITIALIZER("struct S5 { int a, __attribute__((x)) b __attribute__((y)); int c : 2, __attribute__((x)) d : 3 __attribute__((y)); int (__attribute__((x)) m)[2]; };"), C_PREPROCESS_DIALECT_GNU17},
    // #3368: a single redundant group after a typedef or tag name, in the
    // type's position after storage and qualifier words, in a list, around
    // a function whose list sits inside it, and in a typedef.
    {S8_INITIALIZER("typedef int T; T (x); static const T (y) = 2, (z)[2]; T (tg(int a)) { return a; } typedef T (U); U (u), (uf(U a)); extern T (tf(void));"),
     C_PREPROCESS_DIALECT_GNU17},
    {S8_INITIALIZER("struct S { int a; }; struct S (s2), (*s3); struct P { int a; }; struct P (pg(void)) { struct P r = {0}; return r; } union W { int a; }; "
                    "union W (w); enum E { E0 }; enum E (e); const struct S (cs);"),
     C_PREPROCESS_DIALECT_GNU17},
    // Body _Static_assert notes (c_parse_ast_from_tree): noted when the
    // walker's brace frames reach the assertion as a block or member item,
    // not behind a label, `case`, statement expression, type name or a
    // `for` header.
    {S8_INITIALIZER("void sa(int x) { _Static_assert(1, \"a\"); { _Static_assert(2, \"b\"); } struct S { _Static_assert(3, \"c\"); int a; union { _Static_assert(4, \"d\"); int b; } u; } s; (void)s; "
                    "int y = ({ _Static_assert(5, \"e\"); 0; }); (void)y; (void)sizeof(struct { _Static_assert(6, \"f\"); int a; }); "
                    "if (x) { _Static_assert(7, \"g\"); } else { _Static_assert(8, \"h\"); } for (;;) { _Static_assert(9, \"i\"); break; } "
                    "do { _Static_assert(10, \"j\"); } while (0); switch (x) { case 0: { _Static_assert(11, \"k\"); } default: break; } }"),
     C_PREPROCESS_DIALECT_GNU17},
    {S8_INITIALIZER("void sb(int x) { L: _Static_assert(1, \"a\"); switch (x) { case 1: _Static_assert(2, \"b\"); break; } "
                    "for (struct { _Static_assert(3, \"c\"); int a; } t = {0}; t.a;) { } int g(void) { _Static_assert(4, \"d\"); return 0; } (void)g; goto L; }"),
     C_PREPROCESS_DIALECT_GNU17},
    {S8_INITIALIZER("void sc(void) { _Static_assert(1, \"a\" \"b\"); typedef struct { _Static_assert(2, \"c\"); int a; } T; T t = {0}; (void)t; } "
                    "void sd(void) { } _Static_assert(sizeof(int) == 4, \"e\"); _Static_assert(-1 < 0, \"f\"); _Static_assert((1), \"g\");"),
     C_PREPROCESS_DIALECT_GNU17},
    // Declarator shapes the split reads from the chain of derivations: groups
    // two deep, qualified pointers, variadic lists in and out of groups, the
    // old-style identifier-list ambiguity, and initializers whose first token
    // is a parenthesis.
    {S8_INITIALIZER("int *(*(*p3)[2])(void); extern int (*const tbl3[3])(void); int * const * volatile pp3; int (*fpv)(int, ...), xv; "
                    "int fv(int, ...), gv(void); int fw(int a, ...) { return a; } int (*fx(int, ...))(char, ...);"),
     C_PREPROCESS_DIALECT_GNU17},
    {S8_INITIALIZER("typedef int T; int ft(T) { return 0; } int fu(T, T); int fk(a, b) { return 0; } int fe(); int fd(void) { return 0; } int fz(T, int);"),
     C_PREPROCESS_DIALECT_GNU17},
    {S8_INITIALIZER("bool fb(bool) { return 0; } int fc(bool, int); bool;"), C_PREPROCESS_DIALECT_C23},
    {S8_INITIALIZER("struct S6; union { int a; }; enum { A6, B6 = A6 + 1 }; const struct S7 { int a; }; __extension__ struct S8 { int a; }; "
                    "static struct S9 { int a; }; struct { struct S6 *p; int (*f)(void); }; enum __attribute__((packed)) { C6 };"),
     C_PREPROCESS_DIALECT_GNU17},
    {S8_INITIALIZER("int a7 = (1), b7 = ((2)) + 3, *c7 = (int *)0, d7[] = {1, 2}; char *p7 = \"abc\", q7[] = \"de\"; int x7 __asm__(\"y7\") = 1, z7; "
                    "int e7 = -(1), f7 = sizeof (int), g7 = (int){4};"),
     C_PREPROCESS_DIALECT_GNU17},
    {S8_INITIALIZER("int __attribute__((unused)) a8, b8; int f8(void) __attribute__((noreturn)); [[nodiscard]] int g8(void); int h8(void) [[deprecated]];"),
     C_PREPROCESS_DIALECT_C23},
    // A redundant group after a decorated typedef name (#3368).
    {S8_INITIALIZER("typedef int T; __attribute__((unused)) T (x); static __attribute__((unused)) const T (y), (z)[2]; "
                    "__attribute__((unused)) T (tg(int a)) { return a; } __extension__ T (w); int __attribute__((unused)) T2(int);"),
     C_PREPROCESS_DIALECT_GNU17},
    {S8_INITIALIZER("typedef int T; [[maybe_unused]] T (x); [[maybe_unused]] static T (y); [[maybe_unused]] T (tc(int a)) { return a; }"),
     C_PREPROCESS_DIALECT_C23},
};

BUSTER_GLOBAL_LOCAL UnitTestResult c_ast_corpus_constructs_run(UnitTestArguments* arguments, CAstCorpusTally* tally)
{
    UnitTestResult result = {0};
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(c_ast_corpus_constructs); index += 1)
    {
        TemporalArena temporary = scratch_begin(&arguments->arena, 1);
        String8 label = string_format(temporary.arena, S8("construct {u32}: {S8}"), index, c_ast_corpus_constructs[index].source);
        CPreprocessOptions options = {.source_path = S8("construct.c"), .dialect = c_ast_corpus_constructs[index].dialect};
        c_ast_test_merge(&result, c_ast_corpus_unit(arguments, label, c_ast_corpus_constructs[index].source, options, true, tally));
        c_test_scratch_end(temporary);
    }
    return result;
}

// Inputs on which the two passes legitimately differ, each pinned on both
// sides so that a fix to either pass turns the entry into a failure that says
// to move it to the table above.
typedef enum CAstCorpusKnownSide
{
    // c_parse_ast's record is wrong: the tree builds, and its first record
    // differs from c_parse_ast's in `field` (the first one the comparison
    // reports), with `tree_kind` against `old_kind`.
    C_AST_CORPUS_KNOWN_OLD_WRONG,
    // The tree rejects valid GNU/C input that c_parse_ast accepts, with a
    // diagnostic containing `diagnostic`.
    C_AST_CORPUS_KNOWN_TREE_GAP,
} CAstCorpusKnownSide;

typedef struct CAstCorpusKnown CAstCorpusKnown;
struct CAstCorpusKnown
{
    String8 source;
    String8 reason;
    String8 diagnostic;
    String8 field;
    CPreprocessDialect dialect;
    CAstCorpusKnownSide side;
    CParserDeclarationKind old_kind;
    CParserDeclarationKind tree_kind;
};

// No input is pinned today. The all-zero entry ends the table (an empty source
// is not an input), which keeps it a valid array where nothing is pinned; add
// a pinned input before it.
BUSTER_GLOBAL_LOCAL CAstCorpusKnown const c_ast_corpus_known[] = {
    {{0}},
};

BUSTER_GLOBAL_LOCAL UnitTestResult c_ast_corpus_known_run(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    for (u32 index = 0; c_ast_corpus_known[index].source.length; index += 1)
    {
        CAstCorpusKnown const* known = &c_ast_corpus_known[index];
        TemporalArena temporary = scratch_begin(&arguments->arena, 1);
        String8 label = string_format(temporary.arena, S8("known difference {u32}: {S8}"), index, known->source);
        CPreprocessResult preprocess = c_ast_test_preprocess(temporary.arena, known->source, known->dialect);
        if (BUSTER_REQUIRE(arguments, preprocess.error_count == 0))
        {
            CParserResult syntax = c_parse_ast_reference(temporary.arena, preprocess);
            CAstResult built = c_ast_build(temporary.arena, preprocess, (CAstOptions){0});
            BUSTER_TEST_RAW(arguments, syntax.diagnostic_count == 0 && syntax.first_declaration, label);
            if (known->side == C_AST_CORPUS_KNOWN_OLD_WRONG)
            {
                if (BUSTER_REQUIRE(arguments, built.complete && c_ast_validate(&built.ast) == C_AST_NODE_INVALID))
                {
                    u32 record_count = c_ast_corpus_records(temporary.arena, &built.ast, 0);
                    CAstCorpusRecord* records = arena_allocate(temporary.arena, CAstCorpusRecord, record_count + 1);
                    c_ast_corpus_records(temporary.arena, &built.ast, records);
                    BUSTER_TEST_RAW(arguments, record_count > 0 && records[0].kind == known->tree_kind, label);
                    BUSTER_TEST_RAW(arguments, syntax.first_declaration && syntax.first_declaration->kind == known->old_kind, label);
                    String8 mismatch = c_ast_corpus_compare(temporary.arena, &syntax, records, record_count, &built.ast);
                    String8 needle = string_format(temporary.arena, S8(": {S8}: "), known->field);
                    BUSTER_TEST_RAW(arguments, string_first_sequence(mismatch, needle) != BUSTER_STRING_NO_MATCH, label);
                }
            }
            else
            {
                BUSTER_TEST_RAW(arguments, !built.complete && built.diagnostic_count == 1, label);
                if (built.diagnostic_count)
                {
                    BUSTER_TEST_RAW(arguments, string_first_sequence(built.diagnostics[0].message, known->diagnostic) != BUSTER_STRING_NO_MATCH, label);
                }
            }
            c_ast_release(&built.ast);
        }
        c_test_scratch_end(temporary);
    }
    return result;
}

// tests/basic_c_dialect.c in every dialect the driver test compiles it in,
// with the macros that test passes: the dialect gates (C23 keyword spellings,
// `static_assert` and `alignas` without underscores, `thread_local`, `u8`
// literals) change the token classification.
BUSTER_GLOBAL_LOCAL UnitTestResult c_ast_corpus_dialect_matrix(UnitTestArguments* arguments, CAstCorpusTally* tally)
{
    UnitTestResult result = {0};
    CPreprocessDialect dialects[] = {C_PREPROCESS_DIALECT_GNU99, C_PREPROCESS_DIALECT_GNU11, C_PREPROCESS_DIALECT_GNU17, C_PREPROCESS_DIALECT_GNU23,
                                     C_PREPROCESS_DIALECT_C99,   C_PREPROCESS_DIALECT_C11,   C_PREPROCESS_DIALECT_C17,   C_PREPROCESS_DIALECT_C23};
    String8 versions[] = {S8("EXPECTED_STDC_VERSION=199901L"), S8("EXPECTED_STDC_VERSION=201112L"), S8("EXPECTED_STDC_VERSION=201710L"),
                          S8("EXPECTED_STDC_VERSION=202311L"), S8("EXPECTED_STDC_VERSION=199901L"), S8("EXPECTED_STDC_VERSION=201112L"),
                          S8("EXPECTED_STDC_VERSION=201710L"), S8("EXPECTED_STDC_VERSION=202311L")};
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(dialects); index += 1)
    {
        CPreprocessorOperation operations[] = {
            {.operand = versions[index], .kind = C_PREPROCESSOR_OPERATION_DEFINE},
            {.operand = index < 4 ? S8("EXPECTED_GNU=1") : S8("EXPECTED_GNU=0"), .kind = C_PREPROCESSOR_OPERATION_DEFINE},
        };
        CPreprocessOptions options = {
            .macro_operations = operations,
            .macro_operation_count = BUSTER_ARRAY_LENGTH(operations),
            .dialect = dialects[index],
        };
        c_ast_test_merge(&result, c_ast_corpus_file(arguments, S8("tests/basic_c_dialect.c"), options, true, tally));
    }
    return result;
}

#if BUSTER_LINUX && !BUSTER_ANDROID
// The compiler's own C frontend, preprocessed the way `ide cc -Isrc` does:
// real-world input, GNU extensions and system headers included. The system
// include paths are the driver's own defaults (compiler_driver_parse_arguments
// resolves them), so this half needs the host's libc headers; it runs where
// they are installed in the default location and is skipped, loudly, where
// they are not.
BUSTER_GLOBAL_LOCAL UnitTestResult c_ast_corpus_sources(UnitTestArguments* arguments, CAstCorpusTally* tally)
{
    UnitTestResult result = {0};
    String8 paths[] = {
        S8("src/buster/lib/compiler/frontend/c/c_source.c"),
        S8("src/buster/lib/compiler/frontend/c/c_parse.c"),
        S8("src/buster/lib/compiler/frontend/c/c_gen.c"),
        S8("src/buster/lib/compiler/frontend/c/c_ast.c"),
    };
    bool hosted = os_path_followed_stats(S8("/usr/include/stdint.h")).kind == OS_FILE_KIND_REGULAR;
    if (!hosted)
    {
        string_print_error(S8("c_ast_test_corpus: /usr/include/stdint.h is absent; the compiler-source half is skipped\n"));
    }
    u64 built_before = tally->built;
    u64 split_units_before = tally->split_units;
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(paths) && hosted; index += 1)
    {
        TemporalArena temporary = scratch_begin(&arguments->arena, 1);
        String8 command[] = {S8("-Isrc"), S8("-fsyntax-only"), paths[index]};
        CompilerDriverInvocation invocation = compiler_driver_parse_arguments(temporary.arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(command));
        if (BUSTER_REQUIRE(arguments, invocation.error == COMPILER_DRIVER_ERROR_NONE))
        {
            CPreprocessOptions options = {
                .macro_operations = invocation.macro_operations,
                .macro_operation_count = invocation.macro_operation_count,
                .include_paths = invocation.include_paths,
                .include_path_count = invocation.include_path_count,
                .system_include_paths = invocation.system_include_paths,
                .system_include_path_count = invocation.system_include_path_count,
                .dialect = C_PREPROCESS_DIALECT_GNU17,
            };
            c_ast_test_merge(&result, c_ast_corpus_file(arguments, paths[index], options, true, tally));
        }
        c_test_scratch_end(temporary);
    }
    BUSTER_TEST(arguments, !hosted || tally->built == built_before + BUSTER_ARRAY_LENGTH(paths));
    // Real-world input with glibc's headers: every source takes the tree split.
    BUSTER_TEST(arguments, !hosted || tally->split_units == split_units_before + BUSTER_ARRAY_LENGTH(paths));
    BUSTER_TEST(arguments, !hosted || tally->split_records >= C_AST_CORPUS_HOSTED_SPLIT_RECORD_FLOOR);
    BUSTER_TEST(arguments, !hosted || tally->type_answers >= C_AST_CORPUS_HOSTED_TYPE_ANSWER_FLOOR);
    BUSTER_TEST_RAW(arguments, !hosted || tally->type_shared_members >= C_AST_CORPUS_HOSTED_TYPE_SHARED_MEMBER_FLOOR,
                    string_format(arguments->arena, S8("{u64} shared qualified member answers with the frontend's sources, below the floor"),
                                  tally->type_shared_members));
    BUSTER_TEST(arguments, !hosted || (tally->statement_segments >= C_AST_CORPUS_HOSTED_STATEMENT_SEGMENT_FLOOR &&
                                       tally->statement_uses >= C_AST_CORPUS_HOSTED_STATEMENT_USE_FLOOR));
    BUSTER_TEST(arguments, !hosted || tally->model_rows >= C_AST_CORPUS_HOSTED_MODEL_ROW_FLOOR);
    BUSTER_TEST(arguments, !hosted || (tally->statement_declarators >= C_AST_CORPUS_HOSTED_STATEMENT_DECLARATOR_FLOOR &&
                                       tally->statement_initializers >= C_AST_CORPUS_HOSTED_STATEMENT_INITIALIZER_FLOOR &&
                                       tally->statement_declarator_declines == 0));
    BUSTER_TEST(arguments, !hosted || tally->specifier_answers >= C_AST_CORPUS_HOSTED_SPECIFIER_ANSWER_FLOOR);
    BUSTER_TEST(arguments, !hosted || tally->statement_braced >= C_AST_CORPUS_HOSTED_STATEMENT_BRACED_FLOOR);
    BUSTER_TEST(arguments, !hosted || (tally->type_name_skips >= C_AST_CORPUS_HOSTED_TYPE_NAME_SKIP_FLOOR &&
                                       tally->type_name_checked == tally->type_name_skips &&
                                       tally->type_name_answers >= C_AST_CORPUS_HOSTED_TYPE_NAME_ANSWER_FLOOR));
    BUSTER_TEST_RAW(arguments, !hosted || (tally->member_answers >= C_AST_CORPUS_HOSTED_MEMBER_ANSWER_FLOOR && tally->member_compared == tally->member_answers),
                    string_format(arguments->arena, S8("{u64} member runs read with the frontend's sources, {u64} compared"), tally->member_answers,
                                  tally->member_compared));
    return result;
}
#endif

BUSTER_GLOBAL_LOCAL UnitTestResult c_ast_test_corpus(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    CAstCorpusList list = c_ast_corpus_collect(arguments->arena);
    BUSTER_TEST(arguments, list.valid);
    String8 includes[] = {S8("tests")};
    CAstCorpusTally tally = {0};
    for (u32 index = 0; index < list.path_count; index += 1)
    {
        String8 path = list.paths[index];
        if (!string_equal(path, S8("tests/basic_c_dialect.c")))
        {
            CPreprocessOptions options = {
                .include_paths = includes,
                .include_path_count = 1,
                .dialect = c_ast_corpus_in(c_ast_corpus_c23, BUSTER_ARRAY_LENGTH(c_ast_corpus_c23), path) ? C_PREPROCESS_DIALECT_C23
                                                                                                            : C_PREPROCESS_DIALECT_GNU17,
            };
            c_ast_test_merge(&result, c_ast_corpus_file(arguments, path, options, false, &tally));
        }
    }
    c_ast_test_merge(&result, c_ast_corpus_dialect_matrix(arguments, &tally));
    c_ast_test_merge(&result, c_ast_corpus_constructs_run(arguments, &tally));
    c_ast_test_merge(&result, c_ast_corpus_known_run(arguments));
    BUSTER_TEST(arguments, tally.built >= C_AST_CORPUS_BUILT_FLOOR);
    BUSTER_TEST(arguments, tally.skipped <= C_AST_CORPUS_SKIPPED_CEILING);
    BUSTER_TEST(arguments, tally.records >= C_AST_CORPUS_RECORD_FLOOR);
    BUSTER_TEST(arguments, tally.pinned == BUSTER_ARRAY_LENGTH(c_ast_corpus_pins));
    BUSTER_TEST(arguments, tally.type_answers >= C_AST_CORPUS_TYPE_ANSWER_FLOOR && tally.type_compared == tally.type_answers);
    BUSTER_TEST(arguments, tally.type_probes >= C_AST_CORPUS_TYPE_PROBE_FLOOR);
    BUSTER_TEST_RAW(arguments, tally.type_shared_members >= C_AST_CORPUS_TYPE_SHARED_MEMBER_FLOOR,
                    string_format(arguments->arena, S8("{u64} shared qualified member answers, below the floor"), tally.type_shared_members));
    BUSTER_TEST(arguments, tally.split_records >= C_AST_CORPUS_SPLIT_RECORD_FLOOR && tally.split_compared >= tally.split_records);
    BUSTER_TEST(arguments, tally.statement_segments >= C_AST_CORPUS_STATEMENT_SEGMENT_FLOOR && tally.statement_uses >= C_AST_CORPUS_STATEMENT_USE_FLOOR &&
                               tally.statement_compared >= tally.statement_uses && tally.statement_hints);
    BUSTER_TEST(arguments, tally.model_rows >= C_AST_CORPUS_MODEL_ROW_FLOOR);
    BUSTER_TEST(arguments, tally.statement_declarators >= C_AST_CORPUS_STATEMENT_DECLARATOR_FLOOR &&
                               tally.statement_initializers >= C_AST_CORPUS_STATEMENT_INITIALIZER_FLOOR && tally.statement_declarator_declines == 0);
    BUSTER_TEST(arguments, tally.specifier_answers >= C_AST_CORPUS_SPECIFIER_ANSWER_FLOOR && tally.specifier_compared);
    BUSTER_TEST(arguments, tally.statement_braced >= C_AST_CORPUS_STATEMENT_BRACED_FLOOR);
    BUSTER_TEST(arguments, tally.type_name_skips >= C_AST_CORPUS_TYPE_NAME_SKIP_FLOOR && tally.type_name_checked == tally.type_name_skips &&
                               tally.type_name_answers >= C_AST_CORPUS_TYPE_NAME_ANSWER_FLOOR);
    BUSTER_TEST_RAW(arguments, tally.member_answers >= C_AST_CORPUS_MEMBER_ANSWER_FLOOR && tally.member_compared == tally.member_answers,
                    string_format(arguments->arena, S8("{u64} member runs read from the fixtures, {u64} compared"), tally.member_answers,
                                  tally.member_compared));
#if BUSTER_LINUX && !BUSTER_ANDROID
    c_ast_test_merge(&result, c_ast_corpus_sources(arguments, &tally));
#endif
    // Every body of the corpus follows its plan; none is left to the token walk.
    BUSTER_TEST(arguments, tally.statement_fallbacks == 0 && tally.statement_bodies);
    // Every fact candidate is published or counted as a gap once, and no
    // published row outlives the type table (GitHub #4062).
    BUSTER_TEST(arguments, tally.fact_candidates && tally.fact_stale == 0 && tally.fact_unaccounted == 0 && tally.fact_missing < tally.fact_candidates);
    return result;
}

// Rows kept and lowering answers that agree with them over the fixtures, and
// rows with the frontend's own sources: about 108, 101 and 7,120 on Linux
// x86-64 (17 of all the rows from constant contexts, the rest from the
// validation walk's own queries).
enum
{
    C_AST_SIZEOF_HANDOFF_ROW_FLOOR = 90,
    C_AST_SIZEOF_HANDOFF_AGREE_FLOOR = 85,
    C_AST_SIZEOF_HANDOFF_HOSTED_ROW_FLOOR = 6000,
};

// The sizeof handoff comparison (c_test_sizeof_handoff_set, GitHub #2531) over
// the corpus inputs: each input is analysed again with the comparison on and,
// when the analysis is clean, lowered with it on, so every `sizeof` size
// lowering settles is held to the semantic rows for its keyword. Analysis of a
// frontend source with its headers outgrows a test scratch arena, so each
// input gets an arena of its own.
BUSTER_GLOBAL_LOCAL UnitTestResult c_ast_sizeof_handoff_unit(UnitTestArguments* arguments, String8 label, String8 source, CPreprocessOptions options,
                                                             CTestSizeofHandoff* sum, u64* lowered_units)
{
    UnitTestResult result = {0};
    ArenaCreation creation = {.reserved_size = BUSTER_GB(16), .flags = {.no_pool = true}};
    Arena* arena = arena_create(creation);
    if (BUSTER_REQUIRE(arguments, arena != 0))
    {
        options.target = target_native;
        options.data_layout = target_data_layout(target_native);
        CPreprocessResult preprocess = c_preprocess(arena, source, options);
        if (!preprocess.error_count)
        {
            CParserResult syntax = c_parse_ast(arena, preprocess);
            c_test_sizeof_handoff_take();
            c_test_sizeof_handoff_set(true);
            CAnalysisResult analysis = c_analyze_semantics_only(arena, preprocess, syntax);
            bool clean = !syntax.diagnostic_count && analysis.analysis_complete && !analysis.diagnostic_count;
            CIRLowerResult lowered = clean ? c_lower_to_ir(arena, label, preprocess, analysis, target_native) : (CIRLowerResult){0};
            c_test_sizeof_handoff_set(false);
            CTestSizeofHandoff taken = c_test_sizeof_handoff_take();
            *lowered_units += clean && lowered.program && !lowered.diagnostic_count;
            for (u32 producer = 0; producer < C_SIZEOF_OPERAND_PRODUCER_COUNT; producer += 1)
            {
                sum->rows[producer] += taken.rows[producer];
            }
            sum->row_bytes += taken.row_bytes;
            sum->repeats += taken.repeats;
            sum->conflicts += taken.conflicts;
            sum->extra_queries += taken.extra_queries;
            sum->outside_population += taken.outside_population;
            sum->scope_differs += taken.scope_differs;
            sum->signedness_compared += taken.signedness_compared;
            for (u32 path = 0; path < C_TEST_SIZEOF_HANDOFF_PATHS; path += 1)
            {
                for (u32 outcome = 0; outcome < C_TEST_SIZEOF_HANDOFF_OUTCOMES; outcome += 1)
                {
                    sum->outcomes[path][outcome] += taken.outcomes[path][outcome];
                }
            }
            for (u32 index = 0; index < taken.first_count; index += 1)
            {
                CTestSizeofHandoffMismatch const* mismatch = taken.first + index;
                CSourceLocation location = mismatch->keyword < preprocess.token_count
                                               ? c_preprocess_token_location(&preprocess, preprocess.tokens[mismatch->keyword])
                                               : (CSourceLocation){0};
                BUSTER_TEST_RAW(arguments, false,
                                string_format(arguments->arena,
                                              S8("{S8}: sizeof at token {u32} (line {u32}, column {u32}): semantic kind {u32} size {u32} signed {u32} (producer {u32}), "
                                                 "lowering size {u64} signed {u32} (path {u32})"),
                                              label, mismatch->keyword, location.line, location.column, (u32)mismatch->kind, (u32)mismatch->semantic_size,
                                              (u32)mismatch->semantic_signed, (u32)mismatch->producer, mismatch->lowering_size,
                                              (u32)mismatch->lowering_signed, (u32)mismatch->path));
            }
        }
        c_preprocess_release(&preprocess);
        BUSTER_VALIDATE(arena_destroy(arena, 1));
    }
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult c_ast_test_sizeof_handoff(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    CAstCorpusList list = c_ast_corpus_collect(arguments->arena);
    BUSTER_TEST(arguments, list.valid);
    String8 includes[] = {S8("tests")};
    CTestSizeofHandoff sum = {0};
    u64 lowered_units = 0;
    for (u32 index = 0; index < list.path_count; index += 1)
    {
        String8 path = list.paths[index];
        TemporalArena temporary = scratch_begin(&arguments->arena, 1);
        ByteSlice file = string_equal(path, S8("tests/basic_c_dialect.c")) ? (ByteSlice){0} : file_read(temporary.arena, path, (FileReadOptions){0});
        if (file.pointer && file.length)
        {
            CPreprocessOptions options = {
                .source_path = path,
                .include_paths = includes,
                .include_path_count = 1,
                .dialect = c_ast_corpus_in(c_ast_corpus_c23, BUSTER_ARRAY_LENGTH(c_ast_corpus_c23), path) ? C_PREPROCESS_DIALECT_C23
                                                                                                            : C_PREPROCESS_DIALECT_GNU17,
            };
            c_ast_test_merge(&result, c_ast_sizeof_handoff_unit(arguments, path, (String8){.pointer = (char8*)file.pointer, .length = file.length},
                                                                options, &sum, &lowered_units));
        }
        c_test_scratch_end(temporary);
    }
    u64 fixture_rows = sum.rows[C_SIZEOF_OPERAND_PRODUCER_CONSTANT] + sum.rows[C_SIZEOF_OPERAND_PRODUCER_VALIDATION];
    u64 fixture_agree = sum.outcomes[C_TEST_SIZEOF_HANDOFF_PATH_RESOLVER][C_TEST_SIZEOF_HANDOFF_AGREE] +
                        sum.outcomes[C_TEST_SIZEOF_HANDOFF_PATH_PREDICTION][C_TEST_SIZEOF_HANDOFF_AGREE];
    BUSTER_TEST_RAW(arguments, fixture_rows >= C_AST_SIZEOF_HANDOFF_ROW_FLOOR && fixture_agree >= C_AST_SIZEOF_HANDOFF_AGREE_FLOOR,
                    string_format(arguments->arena, S8("sizeof handoff over the fixtures: {u64} rows, {u64} agreeing answers, below the floor"),
                                  fixture_rows, fixture_agree));
#if BUSTER_LINUX && !BUSTER_ANDROID
    String8 paths[] = {
        S8("src/buster/lib/compiler/frontend/c/c_source.c"),
        S8("src/buster/lib/compiler/frontend/c/c_parse.c"),
        S8("src/buster/lib/compiler/frontend/c/c_gen.c"),
        S8("src/buster/lib/compiler/frontend/c/c_ast.c"),
    };
    bool hosted = os_path_followed_stats(S8("/usr/include/stdint.h")).kind == OS_FILE_KIND_REGULAR;
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(paths) && hosted; index += 1)
    {
        TemporalArena temporary = scratch_begin(&arguments->arena, 1);
        String8 command[] = {S8("-Isrc"), S8("-fsyntax-only"), paths[index]};
        CompilerDriverInvocation invocation = compiler_driver_parse_arguments(temporary.arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(command));
        ByteSlice file = file_read(temporary.arena, paths[index], (FileReadOptions){0});
        if (BUSTER_REQUIRE(arguments, invocation.error == COMPILER_DRIVER_ERROR_NONE && file.pointer && file.length))
        {
            CPreprocessOptions options = {
                .source_path = paths[index],
                .macro_operations = invocation.macro_operations,
                .macro_operation_count = invocation.macro_operation_count,
                .include_paths = invocation.include_paths,
                .include_path_count = invocation.include_path_count,
                .system_include_paths = invocation.system_include_paths,
                .system_include_path_count = invocation.system_include_path_count,
                .dialect = C_PREPROCESS_DIALECT_GNU17,
            };
            c_ast_test_merge(&result, c_ast_sizeof_handoff_unit(arguments, paths[index], (String8){.pointer = (char8*)file.pointer, .length = file.length},
                                                                options, &sum, &lowered_units));
        }
        c_test_scratch_end(temporary);
    }
    u64 hosted_rows = sum.rows[C_SIZEOF_OPERAND_PRODUCER_CONSTANT] + sum.rows[C_SIZEOF_OPERAND_PRODUCER_VALIDATION] - fixture_rows;
    BUSTER_TEST_RAW(arguments, !hosted || hosted_rows >= C_AST_SIZEOF_HANDOFF_HOSTED_ROW_FLOOR,
                    string_format(arguments->arena, S8("sizeof handoff over the frontend sources: {u64} rows, below the floor"), hosted_rows));
#endif
    // A key is kept once per producer; a second, different answer for it is a
    // semantic disagreement with itself.
    BUSTER_TEST_RAW(arguments, sum.conflicts == 0, string_format(arguments->arena, S8("{u64} sizeof handoff rows conflict"), sum.conflicts));
    // Each disagreement was reported with its location by the unit above.
    u64 mismatches = 0;
    for (u32 path = 0; path < C_TEST_SIZEOF_HANDOFF_PATHS; path += 1)
    {
        mismatches += sum.outcomes[path][C_TEST_SIZEOF_HANDOFF_SIZE_MISMATCH] + sum.outcomes[path][C_TEST_SIZEOF_HANDOFF_SIGNEDNESS_MISMATCH];
    }
    BUSTER_TEST_RAW(arguments, mismatches == 0, string_format(arguments->arena, S8("{u64} sizeof answers disagree with semantic analysis"), mismatches));
    BUSTER_TEST(arguments, lowered_units != 0);
    return result;
}
#endif

// The independent expected-syntax oracle (#3102): written from c_ast.h and
// the C grammar alone, without the implementation, and cross-checked against
// clang's AST for the expression, declarator, statement and typedef-sensitive
// cases. Every case also runs every refill batch and layout through
// c_ast_test_check.
#define c_ast_oracle_expect_dialect(arguments, dialect, source, expected) c_ast_test_expect_dialect((arguments), (source), (dialect), (expected))
BUSTER_GLOBAL_LOCAL UnitTestResult c_ast_test_oracle(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
        // expressions: every binary operator
        c_ast_test_expect(arguments, S8("void f(void) { a * b; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (multiply (identifier a) (identifier b))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { a / b; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (divide (identifier a) (identifier b))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { a % b; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (remainder (identifier a) (identifier b))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { a + b; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (add (identifier a) (identifier b))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { a - b; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (subtract (identifier a) (identifier b))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { a << b; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (shift_left (identifier a) (identifier b))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { a >> b; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (shift_right (identifier a) (identifier b))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { a < b; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (less (identifier a) (identifier b))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { a > b; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (greater (identifier a) (identifier b))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { a <= b; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (less_equal (identifier a) (identifier b))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { a >= b; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (greater_equal (identifier a) (identifier b))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { a == b; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (equal (identifier a) (identifier b))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { a != b; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (not_equal (identifier a) (identifier b))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { a & b; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (bit_and (identifier a) (identifier b))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { a ^ b; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (bit_xor (identifier a) (identifier b))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { a | b; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (bit_or (identifier a) (identifier b))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { a && b; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (logical_and (identifier a) (identifier b))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { a || b; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (logical_or (identifier a) (identifier b))))))"));

        // expressions: assignment operators and comma
        c_ast_test_expect(arguments, S8("void f(void) { a = b; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (assign (identifier a) (identifier b))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { a *= b; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (multiply_assign (identifier a) (identifier b))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { a /= b; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (divide_assign (identifier a) (identifier b))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { a %= b; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (remainder_assign (identifier a) (identifier b))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { a += b; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (add_assign (identifier a) (identifier b))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { a -= b; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (subtract_assign (identifier a) (identifier b))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { a <<= b; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (shift_left_assign (identifier a) (identifier b))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { a >>= b; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (shift_right_assign (identifier a) (identifier b))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { a &= b; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (bit_and_assign (identifier a) (identifier b))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { a ^= b; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (bit_xor_assign (identifier a) (identifier b))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { a |= b; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (bit_or_assign (identifier a) (identifier b))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { a, b; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (comma (identifier a) (identifier b))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { a, b, c; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (comma (comma (identifier a) (identifier b)) (identifier c))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { a = b = c; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (assign (identifier a) (assign (identifier b) (identifier c)))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { a += b -= c; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (add_assign (identifier a) (subtract_assign (identifier b) (identifier c)))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { a = b += c; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (assign (identifier a) (add_assign (identifier b) (identifier c)))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { a = b, c = d; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (comma (assign (identifier a) (identifier b)) (assign (identifier c) (identifier d)))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { a, b = c; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (comma (identifier a) (assign (identifier b) (identifier c)))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { a = (b, c); }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (assign (identifier a) (comma (identifier b) (identifier c)))))))"));

        // expressions: precedence and associativity
        c_ast_test_expect(arguments, S8("void f(void) { a + b * c; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (add (identifier a) (multiply (identifier b) (identifier c)))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { a * b + c; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (add (multiply (identifier a) (identifier b)) (identifier c))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { a - b - c; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (subtract (subtract (identifier a) (identifier b)) (identifier c))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { a / b / c; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (divide (divide (identifier a) (identifier b)) (identifier c))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { a % b * c; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (multiply (remainder (identifier a) (identifier b)) (identifier c))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { a << b << c; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (shift_left (shift_left (identifier a) (identifier b)) (identifier c))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { a >> b + c; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (shift_right (identifier a) (add (identifier b) (identifier c)))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { a + b << c; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (shift_left (add (identifier a) (identifier b)) (identifier c))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { a < b == c < d; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (equal (less (identifier a) (identifier b)) (less (identifier c) (identifier d)))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { a == b != c; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (not_equal (equal (identifier a) (identifier b)) (identifier c))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { a & b ^ c | d; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (bit_or (bit_xor (bit_and (identifier a) (identifier b)) (identifier c)) (identifier d))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { a | b ^ c & d; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (bit_or (identifier a) (bit_xor (identifier b) (bit_and (identifier c) (identifier d))))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { a & b == c; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (bit_and (identifier a) (equal (identifier b) (identifier c)))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { a && b || c; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (logical_or (logical_and (identifier a) (identifier b)) (identifier c))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { a || b && c; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (logical_or (identifier a) (logical_and (identifier b) (identifier c)))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { a && b && c; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (logical_and (logical_and (identifier a) (identifier b)) (identifier c))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { a || b || c; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (logical_or (logical_or (identifier a) (identifier b)) (identifier c))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { a ? b : c; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (conditional (identifier a) (identifier b) (identifier c))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { a ? b : c ? d : e; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (conditional (identifier a) (identifier b) (conditional (identifier c) (identifier d) (identifier e)))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { a ? b ? c : d : e; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (conditional (identifier a) (conditional (identifier b) (identifier c) (identifier d)) (identifier e))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { a ? b, c : d; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (conditional (identifier a) (comma (identifier b) (identifier c)) (identifier d))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { a || b ? c : d; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (conditional (logical_or (identifier a) (identifier b)) (identifier c) (identifier d))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { a = b ? c : d; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (assign (identifier a) (conditional (identifier b) (identifier c) (identifier d)))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { a ? b = c : d; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (conditional (identifier a) (assign (identifier b) (identifier c)) (identifier d))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { a ?: b; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (conditional_omitted (identifier a) (identifier b))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { a ?: b ?: c; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (conditional_omitted (identifier a) (conditional_omitted (identifier b) (identifier c)))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { a || b ?: c; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (conditional_omitted (logical_or (identifier a) (identifier b)) (identifier c))))))"));

        // expressions: unary and postfix operators
        c_ast_test_expect(arguments, S8("void f(void) { ++a; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (pre_increment (identifier a))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { --a; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (pre_decrement (identifier a))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { &a; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (address (identifier a))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { *a; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (dereference (identifier a))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { +a; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (plus (identifier a))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { -a; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (negate (identifier a))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { ~a; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (bit_not (identifier a))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { !a; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (logical_not (identifier a))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { a++; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (post_increment (identifier a))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { a--; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (post_decrement (identifier a))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { !!a; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (logical_not (logical_not (identifier a)))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { ~-a; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (bit_not (negate (identifier a)))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { -a * b; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (multiply (negate (identifier a)) (identifier b))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { !a == b; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (equal (logical_not (identifier a)) (identifier b))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { a * *b; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (multiply (identifier a) (dereference (identifier b)))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { (*a)++; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (post_increment (dereference (identifier a)))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { !a && b || c; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (logical_or (logical_and (logical_not (identifier a)) (identifier b)) (identifier c))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { a - -b; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (subtract (identifier a) (negate (identifier b)))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { a+++b; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (add (post_increment (identifier a)) (identifier b))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { *p++; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (dereference (post_increment (identifier p)))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { a & &b; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (bit_and (identifier a) (address (identifier b)))))))"));

        // expressions: parentheses produce no node
        c_ast_test_expect(arguments, S8("void f(void) { (a); }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (identifier a)))))"));
        c_ast_test_expect(arguments, S8("void f(void) { ((a)); }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (identifier a)))))"));
        c_ast_test_expect(arguments, S8("void f(void) { (a + b) * c; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (multiply (add (identifier a) (identifier b)) (identifier c))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { a * (b + c); }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (multiply (identifier a) (add (identifier b) (identifier c)))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { (a, b); }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (comma (identifier a) (identifier b))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { (a ? b : c) ? d : e; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (conditional (conditional (identifier a) (identifier b) (identifier c)) (identifier d) (identifier e))))))"));

        // expressions: sizeof and alignof
        c_ast_test_expect(arguments, S8("void f(void) { sizeof x; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (sizeof_expression (identifier x))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { sizeof (x); }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (sizeof_expression (identifier x))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { sizeof (int); }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (sizeof_type (type_name (decl_specifiers (specifier_word int))))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { sizeof (int){1}; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (sizeof_expression (compound_literal (type_name (decl_specifiers (specifier_word int))) (initializer_list (number 1))))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { sizeof(int[3]); }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (sizeof_type (type_name (decl_specifiers (specifier_word int)) (declarator_array (number 3))))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { sizeof (int) * 2; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (multiply (sizeof_type (type_name (decl_specifiers (specifier_word int)))) (number 2))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { sizeof x * 2; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (multiply (sizeof_expression (identifier x)) (number 2))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { sizeof -x; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (sizeof_expression (negate (identifier x)))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { sizeof sizeof x; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (sizeof_expression (sizeof_expression (identifier x)))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { sizeof (x) + 1; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (add (sizeof_expression (identifier x)) (number 1))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { sizeof(int *); }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (sizeof_type (type_name (decl_specifiers (specifier_word int)) (declarator_pointer)))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { sizeof *p; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (sizeof_expression (dereference (identifier p)))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { sizeof (a)[0]; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (sizeof_expression (index (identifier a) (number 0)))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { _Alignof (int); }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (alignof_type (type_name (decl_specifiers (specifier_word int))))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { __alignof__(int); }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (alignof_type (type_name (decl_specifiers (specifier_word int))))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { __alignof__(x); }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (alignof_expression (identifier x))))))"));

        // expressions: GNU __real__ __imag__ __extension__
        c_ast_test_expect(arguments, S8("void f(void) { __real__ a; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (real (identifier a))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { __imag__ a; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (imag (identifier a))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { x = __extension__ 1; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (assign (identifier x) (extension (number 1)))))))"));

        // casts
        c_ast_test_expect(arguments, S8("void f(void) { (int)x; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (cast (type_name (decl_specifiers (specifier_word int))) (identifier x))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { (int)(char)x; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (cast (type_name (decl_specifiers (specifier_word int))) (cast (type_name (decl_specifiers (specifier_word char))) (identifier x)))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { (int)-x; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (cast (type_name (decl_specifiers (specifier_word int))) (negate (identifier x)))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { (int)x + y; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (add (cast (type_name (decl_specifiers (specifier_word int))) (identifier x)) (identifier y))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { (int)x++; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (cast (type_name (decl_specifiers (specifier_word int))) (post_increment (identifier x)))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { -(int)x; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (negate (cast (type_name (decl_specifiers (specifier_word int))) (identifier x)))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { (int *)p; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (cast (type_name (decl_specifiers (specifier_word int)) (declarator_pointer)) (identifier p))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { *(int *)p; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (dereference (cast (type_name (decl_specifiers (specifier_word int)) (declarator_pointer)) (identifier p)))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { (unsigned long)x; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (cast (type_name (decl_specifiers (specifier_word unsigned) (specifier_word long))) (identifier x))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { (const char *)s; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (cast (type_name (decl_specifiers (specifier_word const) (specifier_word char)) (declarator_pointer)) (identifier s))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { (char *const)s; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (cast (type_name (decl_specifiers (specifier_word char)) (declarator_pointer const)) (identifier s))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { ((int)x); }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (cast (type_name (decl_specifiers (specifier_word int))) (identifier x))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { (int)x ? a : b; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (conditional (cast (type_name (decl_specifiers (specifier_word int))) (identifier x)) (identifier a) (identifier b))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { (int (*)(void))p; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (cast (type_name (decl_specifiers (specifier_word int)) (declarator_function (declarator_pointer) (parameter_list (parameter (decl_specifiers (specifier_word void)))))) (identifier p))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { ((void (*)(void))p)(); }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (call (cast (type_name (decl_specifiers (specifier_word void)) (declarator_function (declarator_pointer) (parameter_list (parameter (decl_specifiers (specifier_word void)))))) (identifier p)))))))"));

        // postfix: calls, members, subscripts
        c_ast_test_expect(arguments, S8("void f(void) { f(); }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (call (identifier f))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { f(a, b, c); }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (call (identifier f) (identifier a) (identifier b) (identifier c))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { f(g(a), h(b, c)); }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (call (identifier f) (call (identifier g) (identifier a)) (call (identifier h) (identifier b) (identifier c)))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { f(a ? b : c, d); }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (call (identifier f) (conditional (identifier a) (identifier b) (identifier c)) (identifier d))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { f(1)(2); }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (call (call (identifier f) (number 1)) (number 2))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { (*f)(x); }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (call (dereference (identifier f)) (identifier x))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { a[b]; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (index (identifier a) (identifier b))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { a[b][c]; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (index (index (identifier a) (identifier b)) (identifier c))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { a[b + 1] = c; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (assign (index (identifier a) (add (identifier b) (number 1))) (identifier c))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { *a[1]; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (dereference (index (identifier a) (number 1)))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { a.b; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (member b (identifier a))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { a->b; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (member_arrow b (identifier a))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { a.b->c.d; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (member d (member_arrow c (member b (identifier a))))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { a.b[1].c; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (member c (index (member b (identifier a)) (number 1)))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { f(a).b; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (member b (call (identifier f) (identifier a)))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { a[b](c); }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (call (index (identifier a) (identifier b)) (identifier c))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { (a + b)[0]; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (index (add (identifier a) (identifier b)) (number 0))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { ++a->b; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (pre_increment (member_arrow b (identifier a)))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { a.b(c).d[e]; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (index (member d (call (member b (identifier a)) (identifier c))) (identifier e))))))"));

        // literals
        c_ast_test_expect(arguments, S8("void f(void) { 'a'; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (character 'a')))))"));
        c_ast_test_expect(arguments, S8("void f(void) { '\\n'; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (character '\\n')))))"));
        c_ast_test_expect(arguments, S8("void f(void) { '\\''; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (character '\\'')))))"));
        c_ast_test_expect(arguments, S8("void f(void) { \"abc\"; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (string \"abc\")))))"));
        c_ast_test_expect(arguments, S8("void f(void) { \"a\" \"b\"; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (string \"a\" \"b\")))))"));
        c_ast_test_expect(arguments, S8("void f(void) { \"a\" \"b\" \"c\"; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (string \"a\" \"b\" \"c\")))))"));
        c_ast_test_expect(arguments, S8("void f(void) { L\"w\"; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (string L\"w\")))))"));
        c_ast_test_expect(arguments, S8("void f(void) { 1; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (number 1)))))"));
        c_ast_test_expect(arguments, S8("void f(void) { 0x1F; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (number 0x1F)))))"));
        c_ast_test_expect(arguments, S8("void f(void) { 1.5e3f; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (number 1.5e3f)))))"));
        c_ast_test_expect(arguments, S8("void f(void) { 1e+5; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (number 1e+5)))))"));
        c_ast_test_expect(arguments, S8("void f(void) { f(\"a\" \"b\", \"c\"); }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (call (identifier f) (string \"a\" \"b\") (string \"c\"))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { \"a\\\"b\"; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (string \"a\\\"b\")))))"));

        // compound literals
        c_ast_test_expect(arguments, S8("void f(void) { (struct S){1}.a; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (member a (compound_literal (type_name (decl_specifiers (struct_specifier (tag_name S)))) (initializer_list (number 1))))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { (int){1}; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (compound_literal (type_name (decl_specifiers (specifier_word int))) (initializer_list (number 1)))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { (int[]){1, 2}[0]; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (index (compound_literal (type_name (decl_specifiers (specifier_word int)) (declarator_array)) (initializer_list (number 1) (number 2))) (number 0))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { (int[2]){1, 2}; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (compound_literal (type_name (decl_specifiers (specifier_word int)) (declarator_array (number 2))) (initializer_list (number 1) (number 2)))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { (struct S){.a = 1, .b = 2}; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (compound_literal (type_name (decl_specifiers (struct_specifier (tag_name S)))) (initializer_list (designation (designator_member a) (number 1)) (designation (designator_member b) (number 2))))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { f((struct S){1}); }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (call (identifier f) (compound_literal (type_name (decl_specifiers (struct_specifier (tag_name S)))) (initializer_list (number 1))))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { (int){}; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (compound_literal (type_name (decl_specifiers (specifier_word int))) (initializer_list))))))"));

        // GNU statement expressions, label addresses, _Generic
        c_ast_test_expect(arguments, S8("void f(void) { ({ int y = 1; y; }); }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (statement_expression (compound_statement (declaration (decl_specifiers (specifier_word int)) (init_declarator (declarator_name y) (number 1))) (expression_statement (identifier y))))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { x = ({ 1; }); }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (assign (identifier x) (statement_expression (compound_statement (expression_statement (number 1)))))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { x = ({ int t = a; t * 2; }); }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (assign (identifier x) (statement_expression (compound_statement (declaration (decl_specifiers (specifier_word int)) (init_declarator (declarator_name t) (identifier a))) (expression_statement (multiply (identifier t) (number 2))))))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { x = &&done; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (assign (identifier x) (label_address done))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { a && &&l; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (logical_and (identifier a) (label_address l))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { y = _Generic(x, int: 1, default: 2); }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (assign (identifier y) (generic_selection (identifier x) (generic_association (type_name (decl_specifiers (specifier_word int))) (number 1)) (generic_default (number 2))))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { _Generic(x, char *: 3, int: 4); }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (generic_selection (identifier x) (generic_association (type_name (decl_specifiers (specifier_word char)) (declarator_pointer)) (number 3)) (generic_association (type_name (decl_specifiers (specifier_word int))) (number 4)))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { _Generic(a, const int: 1, unsigned long: 2, default: 3); }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (generic_selection (identifier a) (generic_association (type_name (decl_specifiers (specifier_word const) (specifier_word int))) (number 1)) (generic_association (type_name (decl_specifiers (specifier_word unsigned) (specifier_word long))) (number 2)) (generic_default (number 3)))))))"));

        // builtins with type arguments
        c_ast_test_expect(arguments, S8("void f(void) { __builtin_va_arg(ap, int); }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (call (identifier __builtin_va_arg) (identifier ap) (type_name (decl_specifiers (specifier_word int))))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { __builtin_va_arg(ap, char *); }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (call (identifier __builtin_va_arg) (identifier ap) (type_name (decl_specifiers (specifier_word char)) (declarator_pointer)))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { __builtin_types_compatible_p(int, char *); }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (call (identifier __builtin_types_compatible_p) (type_name (decl_specifiers (specifier_word int))) (type_name (decl_specifiers (specifier_word char)) (declarator_pointer)))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { __builtin_offsetof(struct S, a); }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (call (identifier __builtin_offsetof) (type_name (decl_specifiers (struct_specifier (tag_name S)))) (identifier a))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { __builtin_offsetof(struct S, a.b[2]); }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (call (identifier __builtin_offsetof) (type_name (decl_specifiers (struct_specifier (tag_name S)))) (index (member b (identifier a)) (number 2)))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { __builtin_offsetof(struct S, a[1].b.c); }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (call (identifier __builtin_offsetof) (type_name (decl_specifiers (struct_specifier (tag_name S)))) (member c (member b (index (identifier a) (number 1)))))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { __builtin_expect(a == b, 1); }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (call (identifier __builtin_expect) (equal (identifier a) (identifier b)) (number 1))))))"));

        // declarations: basics
        c_ast_test_expect(arguments, S8("int x;"), S8("(translation_unit (declaration (decl_specifiers (specifier_word int)) (init_declarator (declarator_name x))))"));
        c_ast_test_expect(arguments, S8("int x = 1;"), S8("(translation_unit (declaration (decl_specifiers (specifier_word int)) (init_declarator (declarator_name x) (number 1))))"));
        c_ast_test_expect(arguments, S8("int;"), S8("(translation_unit (declaration (decl_specifiers (specifier_word int))))"));
        c_ast_test_expect(arguments, S8("int a, b;"), S8("(translation_unit (declaration (decl_specifiers (specifier_word int)) (init_declarator (declarator_name a)) (init_declarator (declarator_name b))))"));
        c_ast_test_expect(arguments, S8("int a = 1, b = 2;"), S8("(translation_unit (declaration (decl_specifiers (specifier_word int)) (init_declarator (declarator_name a) (number 1)) (init_declarator (declarator_name b) (number 2))))"));
        c_ast_test_expect(arguments, S8("int a, *b, c[2];"), S8("(translation_unit (declaration (decl_specifiers (specifier_word int)) (init_declarator (declarator_name a)) (init_declarator (declarator_pointer (declarator_name b))) (init_declarator (declarator_array (declarator_name c) (number 2)))))"));
        c_ast_test_expect(arguments, S8("int x; int y;"), S8("(translation_unit (declaration (decl_specifiers (specifier_word int)) (init_declarator (declarator_name x))) (declaration (decl_specifiers (specifier_word int)) (init_declarator (declarator_name y))))"));
        c_ast_test_expect(arguments, S8("int x;; int y;"), S8("(translation_unit (declaration (decl_specifiers (specifier_word int)) (init_declarator (declarator_name x))) (empty_declaration) (declaration (decl_specifiers (specifier_word int)) (init_declarator (declarator_name y))))"));
        c_ast_test_expect(arguments, S8(";"), S8("(translation_unit (empty_declaration))"));
        c_ast_test_expect(arguments, S8(""), S8("(translation_unit)"));
        c_ast_test_expect(arguments, S8("int x = 1, y = x + 1;"), S8("(translation_unit (declaration (decl_specifiers (specifier_word int)) (init_declarator (declarator_name x) (number 1)) (init_declarator (declarator_name y) (add (identifier x) (number 1)))))"));

        // specifiers
        c_ast_test_expect(arguments, S8("unsigned long long int x;"), S8("(translation_unit (declaration (decl_specifiers (specifier_word unsigned) (specifier_word long) (specifier_word long) (specifier_word int)) (init_declarator (declarator_name x))))"));
        c_ast_test_expect(arguments, S8("const volatile int x;"), S8("(translation_unit (declaration (decl_specifiers (specifier_word const) (specifier_word volatile) (specifier_word int)) (init_declarator (declarator_name x))))"));
        c_ast_test_expect(arguments, S8("int const x;"), S8("(translation_unit (declaration (decl_specifiers (specifier_word int) (specifier_word const)) (init_declarator (declarator_name x))))"));
        c_ast_test_expect(arguments, S8("signed char c;"), S8("(translation_unit (declaration (decl_specifiers (specifier_word signed) (specifier_word char)) (init_declarator (declarator_name c))))"));
        c_ast_test_expect(arguments, S8("__const__ int x;"), S8("(translation_unit (declaration (decl_specifiers (specifier_word const) (specifier_word int)) (init_declarator (declarator_name x))))"));
        c_ast_test_expect(arguments, S8("__signed__ char c;"), S8("(translation_unit (declaration (decl_specifiers (specifier_word signed) (specifier_word char)) (init_declarator (declarator_name c))))"));
        c_ast_test_expect(arguments, S8("static const int x = 1;"), S8("(translation_unit (declaration (decl_specifiers (specifier_word static) (specifier_word const) (specifier_word int)) (init_declarator (declarator_name x) (number 1))))"));
        c_ast_test_expect(arguments, S8("extern int x;"), S8("(translation_unit (declaration (decl_specifiers (specifier_word extern) (specifier_word int)) (init_declarator (declarator_name x))))"));
        c_ast_test_expect(arguments, S8("_Bool b;"), S8("(translation_unit (declaration (decl_specifiers (specifier_word _Bool)) (init_declarator (declarator_name b))))"));
        c_ast_test_expect(arguments, S8("_Complex double z;"), S8("(translation_unit (declaration (decl_specifiers (specifier_word _Complex) (specifier_word double)) (init_declarator (declarator_name z))))"));
        c_ast_test_expect(arguments, S8("_Imaginary float z;"), S8("(translation_unit (declaration (decl_specifiers (specifier_word _Imaginary) (specifier_word float)) (init_declarator (declarator_name z))))"));
        c_ast_test_expect(arguments, S8("__int128 x;"), S8("(translation_unit (declaration (decl_specifiers (specifier_word __int128)) (init_declarator (declarator_name x))))"));
        c_ast_test_expect(arguments, S8("__builtin_va_list ap;"), S8("(translation_unit (declaration (decl_specifiers (specifier_word __builtin_va_list)) (init_declarator (declarator_name ap))))"));
        c_ast_test_expect(arguments, S8("_Float16 h;"), S8("(translation_unit (declaration (decl_specifiers (specifier_word _Float16)) (init_declarator (declarator_name h))))"));
        c_ast_test_expect(arguments, S8("__bf16 b;"), S8("(translation_unit (declaration (decl_specifiers (specifier_word __bf16)) (init_declarator (declarator_name b))))"));
        // __fp16 is a type word (Clang's stock f16cintrin.h typedefs vectors of it).
        c_ast_test_expect(arguments, S8("__fp16 h;"), S8("(translation_unit (declaration (decl_specifiers (specifier_word __fp16)) (init_declarator (declarator_name h))))"));
        c_ast_test_expect(arguments, S8("inline int f(void);"), S8("(translation_unit (declaration (decl_specifiers (specifier_word inline) (specifier_word int)) (init_declarator (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))))))"));
        c_ast_test_expect(arguments, S8("__inline__ int f(void);"), S8("(translation_unit (declaration (decl_specifiers (specifier_word inline) (specifier_word int)) (init_declarator (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))))))"));
        c_ast_test_expect(arguments, S8("_Noreturn void f(void);"), S8("(translation_unit (declaration (decl_specifiers (specifier_word _Noreturn) (specifier_word void)) (init_declarator (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))))))"));
        c_ast_test_expect(arguments, S8("_Thread_local int x;"), S8("(translation_unit (declaration (decl_specifiers (specifier_word _Thread_local) (specifier_word int)) (init_declarator (declarator_name x))))"));
        c_ast_test_expect(arguments, S8("__thread int x;"), S8("(translation_unit (declaration (decl_specifiers (specifier_word _Thread_local) (specifier_word int)) (init_declarator (declarator_name x))))"));
        c_ast_test_expect(arguments, S8("_Atomic int x;"), S8("(translation_unit (declaration (decl_specifiers (specifier_word _Atomic) (specifier_word int)) (init_declarator (declarator_name x))))"));
        c_ast_test_expect(arguments, S8("_Atomic(int) x;"), S8("(translation_unit (declaration (decl_specifiers (atomic_specifier (type_name (decl_specifiers (specifier_word int))))) (init_declarator (declarator_name x))))"));
        c_ast_test_expect(arguments, S8("_Atomic(int) *p;"), S8("(translation_unit (declaration (decl_specifiers (atomic_specifier (type_name (decl_specifiers (specifier_word int))))) (init_declarator (declarator_pointer (declarator_name p)))))"));
        c_ast_test_expect(arguments, S8("_Alignas(16) int x;"), S8("(translation_unit (declaration (decl_specifiers (alignas (number 16)) (specifier_word int)) (init_declarator (declarator_name x))))"));
        c_ast_test_expect(arguments, S8("_Alignas(int) char c;"), S8("(translation_unit (declaration (decl_specifiers (alignas (type_name (decl_specifiers (specifier_word int)))) (specifier_word char)) (init_declarator (declarator_name c))))"));
        c_ast_test_expect(arguments, S8("__typeof__(x) y;"), S8("(translation_unit (declaration (decl_specifiers (typeof (identifier x))) (init_declarator (declarator_name y))))"));
        c_ast_test_expect(arguments, S8("__typeof__(int) y;"), S8("(translation_unit (declaration (decl_specifiers (typeof (type_name (decl_specifiers (specifier_word int))))) (init_declarator (declarator_name y))))"));
        c_ast_test_expect(arguments, S8("typeof(x) y;"), S8("(translation_unit (declaration (decl_specifiers (typeof (identifier x))) (init_declarator (declarator_name y))))"));
        c_ast_test_expect(arguments, S8("typeof(int *) p;"), S8("(translation_unit (declaration (decl_specifiers (typeof (type_name (decl_specifiers (specifier_word int)) (declarator_pointer)))) (init_declarator (declarator_name p))))"));
        // #3493: typeof's argument is a full expression, comma included;
        // _Alignas and _BitInt keep their constant-expression argument.
        c_ast_test_expect(arguments, S8("__typeof__(0, p) q;"), S8("(translation_unit (declaration (decl_specifiers (typeof (comma (number 0) (identifier p)))) (init_declarator (declarator_name q))))"));
        c_ast_test_expect(arguments, S8("void f(void) { __typeof__(a ? b : c, p) r; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (declaration (decl_specifiers (typeof (comma (conditional (identifier a) (identifier b) (identifier c)) (identifier p)))) (init_declarator (declarator_name r))))))"));
        c_ast_test_expect_failure(arguments, S8("_Alignas(1, 8) int x;"));
        c_ast_test_expect_failure(arguments, S8("_BitInt(1, 8) x;"));

        // struct, union and enum specifiers
        c_ast_test_expect(arguments, S8("struct S;"), S8("(translation_unit (declaration (decl_specifiers (struct_specifier (tag_name S)))))"));
        c_ast_test_expect(arguments, S8("struct S *p;"), S8("(translation_unit (declaration (decl_specifiers (struct_specifier (tag_name S))) (init_declarator (declarator_pointer (declarator_name p)))))"));
        c_ast_test_expect(arguments, S8("struct S { int a; };"), S8("(translation_unit (declaration (decl_specifiers (struct_specifier (tag_name S) (member_list (member_declaration (decl_specifiers (specifier_word int)) (member_declarator (declarator_name a))))))))"));
        c_ast_test_expect(arguments, S8("struct { int a; } s;"), S8("(translation_unit (declaration (decl_specifiers (struct_specifier (member_list (member_declaration (decl_specifiers (specifier_word int)) (member_declarator (declarator_name a)))))) (init_declarator (declarator_name s))))"));
        c_ast_test_expect(arguments, S8("union U { int a; float b; } u;"), S8("(translation_unit (declaration (decl_specifiers (union_specifier (tag_name U) (member_list (member_declaration (decl_specifiers (specifier_word int)) (member_declarator (declarator_name a))) (member_declaration (decl_specifiers (specifier_word float)) (member_declarator (declarator_name b)))))) (init_declarator (declarator_name u))))"));
        c_ast_test_expect(arguments, S8("struct S {};"), S8("(translation_unit (declaration (decl_specifiers (struct_specifier (tag_name S) (member_list)))))"));
        c_ast_test_expect(arguments, S8("struct S { int a, *b, c[2]; };"), S8("(translation_unit (declaration (decl_specifiers (struct_specifier (tag_name S) (member_list (member_declaration (decl_specifiers (specifier_word int)) (member_declarator (declarator_name a)) (member_declarator (declarator_pointer (declarator_name b))) (member_declarator (declarator_array (declarator_name c) (number 2)))))))))"));
        c_ast_test_expect(arguments, S8("struct S { int a : 3, : 0; };"), S8("(translation_unit (declaration (decl_specifiers (struct_specifier (tag_name S) (member_list (member_declaration (decl_specifiers (specifier_word int)) (member_declarator (declarator_name a) (number 3)) (member_declarator (number 0))))))))"));
        c_ast_test_expect(arguments, S8("struct S { unsigned a : 1; signed b : 2; };"), S8("(translation_unit (declaration (decl_specifiers (struct_specifier (tag_name S) (member_list (member_declaration (decl_specifiers (specifier_word unsigned)) (member_declarator (declarator_name a) (number 1))) (member_declaration (decl_specifiers (specifier_word signed)) (member_declarator (declarator_name b) (number 2))))))))"));
        c_ast_test_expect(arguments, S8("struct S { struct { int a; }; int b; };"), S8("(translation_unit (declaration (decl_specifiers (struct_specifier (tag_name S) (member_list (member_declaration (decl_specifiers (struct_specifier (member_list (member_declaration (decl_specifiers (specifier_word int)) (member_declarator (declarator_name a))))))) (member_declaration (decl_specifiers (specifier_word int)) (member_declarator (declarator_name b))))))))"));
        c_ast_test_expect(arguments, S8("struct S { struct T { int x; } t; };"), S8("(translation_unit (declaration (decl_specifiers (struct_specifier (tag_name S) (member_list (member_declaration (decl_specifiers (struct_specifier (tag_name T) (member_list (member_declaration (decl_specifiers (specifier_word int)) (member_declarator (declarator_name x)))))) (member_declarator (declarator_name t))))))))"));
        c_ast_test_expect(arguments, S8("struct S { int a; _Static_assert(1, \"m\"); int b; };"), S8("(translation_unit (declaration (decl_specifiers (struct_specifier (tag_name S) (member_list (member_declaration (decl_specifiers (specifier_word int)) (member_declarator (declarator_name a))) (static_assert (number 1) (string \"m\")) (member_declaration (decl_specifiers (specifier_word int)) (member_declarator (declarator_name b))))))))"));
        c_ast_test_expect(arguments, S8("struct S { int a __attribute__((aligned(8))); };"), S8("(translation_unit (declaration (decl_specifiers (struct_specifier (tag_name S) (member_list (member_declaration (decl_specifiers (specifier_word int)) (member_declarator (declarator_name a) (attribute_list (attribute_gnu (attribute aligned (number 8)))))))))))"));
        c_ast_test_expect(arguments, S8("struct S { int a : 3 __attribute__((unused)); };"), S8("(translation_unit (declaration (decl_specifiers (struct_specifier (tag_name S) (member_list (member_declaration (decl_specifiers (specifier_word int)) (member_declarator (declarator_name a) (number 3) (attribute_list (attribute_gnu (attribute unused))))))))))"));
        c_ast_test_expect(arguments, S8("struct S { void (*fp)(int); int (*arr)[3]; };"), S8("(translation_unit (declaration (decl_specifiers (struct_specifier (tag_name S) (member_list (member_declaration (decl_specifiers (specifier_word void)) (member_declarator (declarator_function (declarator_pointer (declarator_name fp)) (parameter_list (parameter (decl_specifiers (specifier_word int))))))) (member_declaration (decl_specifiers (specifier_word int)) (member_declarator (declarator_array (declarator_pointer (declarator_name arr)) (number 3)))))))))"));
        c_ast_test_expect(arguments, S8("struct S { int a[]; };"), S8("(translation_unit (declaration (decl_specifiers (struct_specifier (tag_name S) (member_list (member_declaration (decl_specifiers (specifier_word int)) (member_declarator (declarator_array (declarator_name a)))))))))"));
        c_ast_test_expect(arguments, S8("struct S { struct S *next; };"), S8("(translation_unit (declaration (decl_specifiers (struct_specifier (tag_name S) (member_list (member_declaration (decl_specifiers (struct_specifier (tag_name S))) (member_declarator (declarator_pointer (declarator_name next)))))))))"));
        c_ast_test_expect(arguments, S8("struct __attribute__((packed)) S { int a; };"), S8("(translation_unit (declaration (decl_specifiers (struct_specifier (attribute_list (attribute_gnu (attribute packed))) (tag_name S) (member_list (member_declaration (decl_specifiers (specifier_word int)) (member_declarator (declarator_name a))))))))"));
        c_ast_test_expect(arguments, S8("struct S { int a; } __attribute__((packed));"), S8("(translation_unit (declaration (decl_specifiers (struct_specifier (tag_name S) (member_list (member_declaration (decl_specifiers (specifier_word int)) (member_declarator (declarator_name a)))) (attribute_list (attribute_gnu (attribute packed)))))))"));
        c_ast_test_expect(arguments, S8("struct S { int a; } __attribute__((packed)) s;"), S8("(translation_unit (declaration (decl_specifiers (struct_specifier (tag_name S) (member_list (member_declaration (decl_specifiers (specifier_word int)) (member_declarator (declarator_name a)))) (attribute_list (attribute_gnu (attribute packed))))) (init_declarator (declarator_name s))))"));
        c_ast_test_expect(arguments, S8("struct S { int a; } x, *y, z[3];"), S8("(translation_unit (declaration (decl_specifiers (struct_specifier (tag_name S) (member_list (member_declaration (decl_specifiers (specifier_word int)) (member_declarator (declarator_name a)))))) (init_declarator (declarator_name x)) (init_declarator (declarator_pointer (declarator_name y))) (init_declarator (declarator_array (declarator_name z) (number 3)))))"));
        c_ast_test_expect(arguments, S8("typedef struct { int a; } S;"), S8("(translation_unit (declaration (decl_specifiers (specifier_word typedef) (struct_specifier (member_list (member_declaration (decl_specifiers (specifier_word int)) (member_declarator (declarator_name a)))))) (init_declarator (declarator_name S))))"));
        c_ast_test_expect(arguments, S8("typedef struct S S;"), S8("(translation_unit (declaration (decl_specifiers (specifier_word typedef) (struct_specifier (tag_name S))) (init_declarator (declarator_name S))))"));
        c_ast_test_expect(arguments, S8("struct S f(void);"), S8("(translation_unit (declaration (decl_specifiers (struct_specifier (tag_name S))) (init_declarator (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))))))"));
        c_ast_test_expect(arguments, S8("enum E { A, B = 2, C };"), S8("(translation_unit (declaration (decl_specifiers (enum_specifier (tag_name E) (enumerator_list (enumerator A) (enumerator B (number 2)) (enumerator C))))))"));
        c_ast_test_expect(arguments, S8("enum E { A, };"), S8("(translation_unit (declaration (decl_specifiers (enum_specifier (tag_name E) (enumerator_list (enumerator A))))))"));
        c_ast_test_expect(arguments, S8("enum { A = 1 << 2, B = A + 1, C = sizeof(int) };"), S8("(translation_unit (declaration (decl_specifiers (enum_specifier (enumerator_list (enumerator A (shift_left (number 1) (number 2))) (enumerator B (add (identifier A) (number 1))) (enumerator C (sizeof_type (type_name (decl_specifiers (specifier_word int))))))))))"));
        c_ast_test_expect(arguments, S8("enum __attribute__((packed)) E { A };"), S8("(translation_unit (declaration (decl_specifiers (enum_specifier (attribute_list (attribute_gnu (attribute packed))) (tag_name E) (enumerator_list (enumerator A))))))"));
        c_ast_test_expect(arguments, S8("enum { A __attribute__((unused)) = 1 };"), S8("(translation_unit (declaration (decl_specifiers (enum_specifier (enumerator_list (enumerator A (attribute_list (attribute_gnu (attribute unused))) (number 1)))))))"));
        c_ast_test_expect(arguments, S8("enum { A __attribute__((deprecated)), B };"), S8("(translation_unit (declaration (decl_specifiers (enum_specifier (enumerator_list (enumerator A (attribute_list (attribute_gnu (attribute deprecated)))) (enumerator B))))))"));
        c_ast_test_expect(arguments, S8("enum E { A } __attribute__((packed));"), S8("(translation_unit (declaration (decl_specifiers (enum_specifier (tag_name E) (enumerator_list (enumerator A)) (attribute_list (attribute_gnu (attribute packed)))))))"));
        c_ast_test_expect(arguments, S8("typedef enum { A, B } E;"), S8("(translation_unit (declaration (decl_specifiers (specifier_word typedef) (enum_specifier (enumerator_list (enumerator A) (enumerator B)))) (init_declarator (declarator_name E))))"));

        // declarators: pointers
        c_ast_test_expect(arguments, S8("int *p;"), S8("(translation_unit (declaration (decl_specifiers (specifier_word int)) (init_declarator (declarator_pointer (declarator_name p)))))"));
        c_ast_test_expect(arguments, S8("int **p;"), S8("(translation_unit (declaration (decl_specifiers (specifier_word int)) (init_declarator (declarator_pointer (declarator_pointer (declarator_name p))))))"));
        c_ast_test_expect(arguments, S8("int *const p;"), S8("(translation_unit (declaration (decl_specifiers (specifier_word int)) (init_declarator (declarator_pointer const (declarator_name p)))))"));
        c_ast_test_expect(arguments, S8("int *const *p;"), S8("(translation_unit (declaration (decl_specifiers (specifier_word int)) (init_declarator (declarator_pointer const (declarator_pointer (declarator_name p))))))"));
        c_ast_test_expect(arguments, S8("int * const * volatile p;"), S8("(translation_unit (declaration (decl_specifiers (specifier_word int)) (init_declarator (declarator_pointer const (declarator_pointer volatile (declarator_name p))))))"));
        c_ast_test_expect(arguments, S8("int *volatile *const *restrict p;"), S8("(translation_unit (declaration (decl_specifiers (specifier_word int)) (init_declarator (declarator_pointer volatile (declarator_pointer const (declarator_pointer restrict (declarator_name p)))))))"));
        c_ast_test_expect(arguments, S8("int *__restrict__ p;"), S8("(translation_unit (declaration (decl_specifiers (specifier_word int)) (init_declarator (declarator_pointer restrict (declarator_name p)))))"));
        c_ast_test_expect(arguments, S8("int * _Atomic p;"), S8("(translation_unit (declaration (decl_specifiers (specifier_word int)) (init_declarator (declarator_pointer _Atomic (declarator_name p)))))"));
        c_ast_test_expect(arguments, S8("int * _Nonnull p;"), S8("(translation_unit (declaration (decl_specifiers (specifier_word int)) (init_declarator (declarator_pointer _Nonnull (declarator_name p)))))"));
        c_ast_test_expect(arguments, S8("int * _Nullable p;"), S8("(translation_unit (declaration (decl_specifiers (specifier_word int)) (init_declarator (declarator_pointer _Nullable (declarator_name p)))))"));
        c_ast_test_expect(arguments, S8("int * __attribute__((unused)) p;"), S8("(translation_unit (declaration (decl_specifiers (specifier_word int)) (init_declarator (declarator_pointer (attribute_list (attribute_gnu (attribute unused))) (declarator_name p)))))"));
        c_ast_test_expect(arguments, S8("int * const __attribute__((unused)) *q;"), S8("(translation_unit (declaration (decl_specifiers (specifier_word int)) (init_declarator (declarator_pointer const (attribute_list (attribute_gnu (attribute unused))) (declarator_pointer (declarator_name q))))))"));
        c_ast_test_expect(arguments, S8("int (*p);"), S8("(translation_unit (declaration (decl_specifiers (specifier_word int)) (init_declarator (declarator_pointer (declarator_name p)))))"));
        c_ast_test_expect(arguments, S8("int (x);"), S8("(translation_unit (declaration (decl_specifiers (specifier_word int)) (init_declarator (declarator_name x))))"));
        c_ast_test_expect(arguments, S8("int *(p);"), S8("(translation_unit (declaration (decl_specifiers (specifier_word int)) (init_declarator (declarator_pointer (declarator_name p)))))"));

        // declarators: arrays
        c_ast_test_expect(arguments, S8("int a[3];"), S8("(translation_unit (declaration (decl_specifiers (specifier_word int)) (init_declarator (declarator_array (declarator_name a) (number 3)))))"));
        c_ast_test_expect(arguments, S8("int a[3][4];"), S8("(translation_unit (declaration (decl_specifiers (specifier_word int)) (init_declarator (declarator_array (declarator_array (declarator_name a) (number 3)) (number 4)))))"));
        c_ast_test_expect(arguments, S8("int a[];"), S8("(translation_unit (declaration (decl_specifiers (specifier_word int)) (init_declarator (declarator_array (declarator_name a)))))"));
        c_ast_test_expect(arguments, S8("int *a[3];"), S8("(translation_unit (declaration (decl_specifiers (specifier_word int)) (init_declarator (declarator_pointer (declarator_array (declarator_name a) (number 3))))))"));
        c_ast_test_expect(arguments, S8("int (*a)[3];"), S8("(translation_unit (declaration (decl_specifiers (specifier_word int)) (init_declarator (declarator_array (declarator_pointer (declarator_name a)) (number 3)))))"));
        c_ast_test_expect(arguments, S8("int (*a[3])[4];"), S8("(translation_unit (declaration (decl_specifiers (specifier_word int)) (init_declarator (declarator_array (declarator_pointer (declarator_array (declarator_name a) (number 3))) (number 4)))))"));

        // declarators: functions and function pointers
        c_ast_test_expect(arguments, S8("void f(void);"), S8("(translation_unit (declaration (decl_specifiers (specifier_word void)) (init_declarator (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))))))"));
        c_ast_test_expect(arguments, S8("void f();"), S8("(translation_unit (declaration (decl_specifiers (specifier_word void)) (init_declarator (declarator_function (declarator_name f) (parameter_list)))))"));
        c_ast_test_expect(arguments, S8("int f(int);"), S8("(translation_unit (declaration (decl_specifiers (specifier_word int)) (init_declarator (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word int))))))))"));
        c_ast_test_expect(arguments, S8("int f(int a, char *b);"), S8("(translation_unit (declaration (decl_specifiers (specifier_word int)) (init_declarator (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word int)) (declarator_name a)) (parameter (decl_specifiers (specifier_word char)) (declarator_pointer (declarator_name b))))))))"));
        c_ast_test_expect(arguments, S8("int f(int, ...);"), S8("(translation_unit (declaration (decl_specifiers (specifier_word int)) (init_declarator (declarator_function (declarator_name f) (parameter_list_variadic (parameter (decl_specifiers (specifier_word int))))))))"));
        c_ast_test_expect(arguments, S8("void f(int *, ...);"), S8("(translation_unit (declaration (decl_specifiers (specifier_word void)) (init_declarator (declarator_function (declarator_name f) (parameter_list_variadic (parameter (decl_specifiers (specifier_word int)) (declarator_pointer)))))))"));
        c_ast_test_expect(arguments, S8("int *f(void);"), S8("(translation_unit (declaration (decl_specifiers (specifier_word int)) (init_declarator (declarator_pointer (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void)))))))))"));
        c_ast_test_expect(arguments, S8("int (f)(void);"), S8("(translation_unit (declaration (decl_specifiers (specifier_word int)) (init_declarator (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))))))"));
        c_ast_test_expect(arguments, S8("int f(void), g(int), x;"), S8("(translation_unit (declaration (decl_specifiers (specifier_word int)) (init_declarator (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void)))))) (init_declarator (declarator_function (declarator_name g) (parameter_list (parameter (decl_specifiers (specifier_word int)))))) (init_declarator (declarator_name x))))"));
        c_ast_test_expect(arguments, S8("int (*fp)(int);"), S8("(translation_unit (declaration (decl_specifiers (specifier_word int)) (init_declarator (declarator_function (declarator_pointer (declarator_name fp)) (parameter_list (parameter (decl_specifiers (specifier_word int))))))))"));
        c_ast_test_expect(arguments, S8("int (*a[3])(void);"), S8("(translation_unit (declaration (decl_specifiers (specifier_word int)) (init_declarator (declarator_function (declarator_pointer (declarator_array (declarator_name a) (number 3))) (parameter_list (parameter (decl_specifiers (specifier_word void))))))))"));
        c_ast_test_expect(arguments, S8("int (*(*f)(int))[2];"), S8("(translation_unit (declaration (decl_specifiers (specifier_word int)) (init_declarator (declarator_array (declarator_pointer (declarator_function (declarator_pointer (declarator_name f)) (parameter_list (parameter (decl_specifiers (specifier_word int)))))) (number 2)))))"));
        c_ast_test_expect(arguments, S8("int (*f(void))(void);"), S8("(translation_unit (declaration (decl_specifiers (specifier_word int)) (init_declarator (declarator_function (declarator_pointer (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void)))))) (parameter_list (parameter (decl_specifiers (specifier_word void))))))))"));
        c_ast_test_expect(arguments, S8("void (*signal(int, void (*)(int)))(int);"), S8("(translation_unit (declaration (decl_specifiers (specifier_word void)) (init_declarator (declarator_function (declarator_pointer (declarator_function (declarator_name signal) (parameter_list (parameter (decl_specifiers (specifier_word int))) (parameter (decl_specifiers (specifier_word void)) (declarator_function (declarator_pointer) (parameter_list (parameter (decl_specifiers (specifier_word int))))))))) (parameter_list (parameter (decl_specifiers (specifier_word int))))))))"));
        c_ast_test_expect(arguments, S8("int (*fp)(int (*)(char), ...);"), S8("(translation_unit (declaration (decl_specifiers (specifier_word int)) (init_declarator (declarator_function (declarator_pointer (declarator_name fp)) (parameter_list_variadic (parameter (decl_specifiers (specifier_word int)) (declarator_function (declarator_pointer) (parameter_list (parameter (decl_specifiers (specifier_word char)))))))))))"));

        // declarators: attribute lists on later declarators (both slots), and opening a parenthesized declarator that is not a pointer
        c_ast_test_expect(arguments, S8("int a, __attribute__((x)) b __attribute__((y));"),
                          S8("(translation_unit (declaration (decl_specifiers (specifier_word int)) (init_declarator (declarator_name a)) (init_declarator (attribute_list (attribute_gnu (attribute x))) (declarator_name b) (attribute_list (attribute_gnu (attribute y))))))"));
        c_ast_test_expect(arguments, S8("int a, __attribute__((x)) b __attribute__((y)) = 1;"),
                          S8("(translation_unit (declaration (decl_specifiers (specifier_word int)) (init_declarator (declarator_name a)) (init_declarator (attribute_list (attribute_gnu (attribute x))) (declarator_name b) (attribute_list (attribute_gnu (attribute y))) (number 1))))"));
        c_ast_test_expect(arguments, S8("int a, __attribute__((x)) b __asm__(\"q\") __attribute__((y)) = 1;"),
                          S8("(translation_unit (declaration (decl_specifiers (specifier_word int)) (init_declarator (declarator_name a)) (init_declarator (attribute_list (attribute_gnu (attribute x))) (declarator_name b) (asm_label (string \"q\")) (attribute_list (attribute_gnu (attribute y))) (number 1))))"));
        c_ast_test_expect(arguments, S8("int a, __attribute__((x)) * __attribute__((z)) b __attribute__((y));"),
                          S8("(translation_unit (declaration (decl_specifiers (specifier_word int)) (init_declarator (declarator_name a)) (init_declarator (attribute_list (attribute_gnu (attribute x))) (declarator_pointer (attribute_list (attribute_gnu (attribute z))) (declarator_name b)) (attribute_list (attribute_gnu (attribute y))))))"));
        c_ast_test_expect(arguments, S8("int a, __attribute__((x)) f(void) __attribute__((y));"),
                          S8("(translation_unit (declaration (decl_specifiers (specifier_word int)) (init_declarator (declarator_name a)) (init_declarator (attribute_list (attribute_gnu (attribute x))) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (attribute_list (attribute_gnu (attribute y))))))"));
        c_ast_test_expect(arguments, S8("int a, __attribute__((x)) __attribute__((w)) b __attribute__((y)) __attribute__((z));"),
                          S8("(translation_unit (declaration (decl_specifiers (specifier_word int)) (init_declarator (declarator_name a)) (init_declarator (attribute_list (attribute_gnu (attribute x)) (attribute_gnu (attribute w))) (declarator_name b) (attribute_list (attribute_gnu (attribute y)) (attribute_gnu (attribute z))))))"));
        c_ast_test_expect(arguments, S8("typedef int T, __attribute__((x)) U __attribute__((y)); U u;"),
                          S8("(translation_unit (declaration (decl_specifiers (specifier_word typedef) (specifier_word int)) (init_declarator (declarator_name T)) (init_declarator (attribute_list (attribute_gnu (attribute x))) (declarator_name U) (attribute_list (attribute_gnu (attribute y))))) (declaration (decl_specifiers (typedef_name U)) (init_declarator (declarator_name u))))"));
        c_ast_test_expect(arguments, S8("void f(void) { int a, __attribute__((x)) b __attribute__((y)) = 1; }"),
                          S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (declaration (decl_specifiers (specifier_word int)) (init_declarator (declarator_name a)) (init_declarator (attribute_list (attribute_gnu (attribute x))) (declarator_name b) (attribute_list (attribute_gnu (attribute y))) (number 1))))))"));
        c_ast_test_expect(arguments, S8("struct S { int a, __attribute__((x)) b; };"),
                          S8("(translation_unit (declaration (decl_specifiers (struct_specifier (tag_name S) (member_list (member_declaration (decl_specifiers (specifier_word int)) (member_declarator (declarator_name a)) (member_declarator (attribute_list (attribute_gnu (attribute x))) (declarator_name b))))))))"));
        c_ast_test_expect(arguments, S8("struct S { int a, __attribute__((x)) b __attribute__((y)); };"),
                          S8("(translation_unit (declaration (decl_specifiers (struct_specifier (tag_name S) (member_list (member_declaration (decl_specifiers (specifier_word int)) (member_declarator (declarator_name a)) (member_declarator (attribute_list (attribute_gnu (attribute x))) (declarator_name b) (attribute_list (attribute_gnu (attribute y))))))))))"));
        c_ast_test_expect(arguments, S8("struct S { int a, __attribute__((x)) b : 3 __attribute__((y)); };"),
                          S8("(translation_unit (declaration (decl_specifiers (struct_specifier (tag_name S) (member_list (member_declaration (decl_specifiers (specifier_word int)) (member_declarator (declarator_name a)) (member_declarator (attribute_list (attribute_gnu (attribute x))) (declarator_name b) (number 3) (attribute_list (attribute_gnu (attribute y))))))))))"));
        c_ast_test_expect(arguments, S8("struct S { int a, __attribute__((x)) : 3 __attribute__((y)); };"),
                          S8("(translation_unit (declaration (decl_specifiers (struct_specifier (tag_name S) (member_list (member_declaration (decl_specifiers (specifier_word int)) (member_declarator (declarator_name a)) (member_declarator (attribute_list (attribute_gnu (attribute x))) (number 3) (attribute_list (attribute_gnu (attribute y))))))))))"));
        c_ast_test_expect(arguments, S8("struct S { int a, __attribute__((x)) *b __attribute__((y)); };"),
                          S8("(translation_unit (declaration (decl_specifiers (struct_specifier (tag_name S) (member_list (member_declaration (decl_specifiers (specifier_word int)) (member_declarator (declarator_name a)) (member_declarator (attribute_list (attribute_gnu (attribute x))) (declarator_pointer (declarator_name b)) (attribute_list (attribute_gnu (attribute y))))))))))"));
        c_ast_test_expect(arguments, S8("int (__attribute__((x)) p);"),
                          S8("(translation_unit (declaration (decl_specifiers (specifier_word int)) (init_declarator (declarator_attributed (attribute_list (attribute_gnu (attribute x))) (declarator_name p)))))"));
        c_ast_test_expect(arguments, S8("int (__attribute__((x)) f)(void);"),
                          S8("(translation_unit (declaration (decl_specifiers (specifier_word int)) (init_declarator (declarator_function (declarator_attributed (attribute_list (attribute_gnu (attribute x))) (declarator_name f)) (parameter_list (parameter (decl_specifiers (specifier_word void))))))))"));
        c_ast_test_expect(arguments, S8("int (__attribute__((x)) a)[3];"),
                          S8("(translation_unit (declaration (decl_specifiers (specifier_word int)) (init_declarator (declarator_array (declarator_attributed (attribute_list (attribute_gnu (attribute x))) (declarator_name a)) (number 3)))))"));
        c_ast_test_expect(arguments, S8("int (__attribute__((x)) p[3]);"),
                          S8("(translation_unit (declaration (decl_specifiers (specifier_word int)) (init_declarator (declarator_attributed (attribute_list (attribute_gnu (attribute x))) (declarator_array (declarator_name p) (number 3))))))"));
        c_ast_test_expect(arguments, S8("int (__attribute__((x)) (*p))(void);"),
                          S8("(translation_unit (declaration (decl_specifiers (specifier_word int)) (init_declarator (declarator_function (declarator_attributed (attribute_list (attribute_gnu (attribute x))) (declarator_pointer (declarator_name p))) (parameter_list (parameter (decl_specifiers (specifier_word void))))))))"));
        c_ast_test_expect(arguments, S8("int (__attribute__((x)) __attribute__((y)) p);"),
                          S8("(translation_unit (declaration (decl_specifiers (specifier_word int)) (init_declarator (declarator_attributed (attribute_list (attribute_gnu (attribute x)) (attribute_gnu (attribute y))) (declarator_name p)))))"));
        c_ast_test_expect(arguments, S8("int (__attribute__((x)) (__attribute__((y)) p));"),
                          S8("(translation_unit (declaration (decl_specifiers (specifier_word int)) (init_declarator (declarator_attributed (attribute_list (attribute_gnu (attribute x))) (declarator_attributed (attribute_list (attribute_gnu (attribute y))) (declarator_name p))))))"));
        c_ast_test_expect(arguments, S8("int (__attribute__((x)) a), (__attribute__((y)) b) = 1;"),
                          S8("(translation_unit (declaration (decl_specifiers (specifier_word int)) (init_declarator (declarator_attributed (attribute_list (attribute_gnu (attribute x))) (declarator_name a))) (init_declarator (declarator_attributed (attribute_list (attribute_gnu (attribute y))) (declarator_name b)) (number 1))))"));
        c_ast_test_expect(arguments, S8("int *(__attribute__((x)) p);"),
                          S8("(translation_unit (declaration (decl_specifiers (specifier_word int)) (init_declarator (declarator_pointer (declarator_attributed (attribute_list (attribute_gnu (attribute x))) (declarator_name p))))))"));
        c_ast_test_expect(arguments, S8("int (__attribute__((x)) p) __attribute__((y));"),
                          S8("(translation_unit (declaration (decl_specifiers (specifier_word int)) (init_declarator (declarator_attributed (attribute_list (attribute_gnu (attribute x))) (declarator_name p)) (attribute_list (attribute_gnu (attribute y))))))"));
        c_ast_test_expect(arguments, S8("int (__attribute__((x)) *p);"),
                          S8("(translation_unit (declaration (decl_specifiers (specifier_word int)) (init_declarator (declarator_pointer (attribute_list (attribute_gnu (attribute x))) (declarator_name p)))))"));
        c_ast_test_expect(arguments, S8("int (__attribute__((x)) fd)(void) { return 0; }"),
                          S8("(translation_unit (function_definition (decl_specifiers (specifier_word int)) (declarator_function (declarator_attributed (attribute_list (attribute_gnu (attribute x))) (declarator_name fd)) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (return (number 0)))))"));
        c_ast_test_expect(arguments, S8("int (__attribute__((x)) (*pq))(int) = 0;"),
                          S8("(translation_unit (declaration (decl_specifiers (specifier_word int)) (init_declarator (declarator_function (declarator_attributed (attribute_list (attribute_gnu (attribute x))) (declarator_pointer (declarator_name pq))) (parameter_list (parameter (decl_specifiers (specifier_word int))))) (number 0))))"));
        c_ast_test_expect(arguments, S8("int (__attribute__((x)) gd(int a)) { return a; }"),
                          S8("(translation_unit (function_definition (decl_specifiers (specifier_word int)) (declarator_attributed (attribute_list (attribute_gnu (attribute x))) (declarator_function (declarator_name gd) (parameter_list (parameter (decl_specifiers (specifier_word int)) (declarator_name a))))) (compound_statement (return (identifier a)))))"));
        c_ast_test_expect(arguments, S8("int (__attribute__((x)) fk)(a) int a; { return a; }"),
                          S8("(translation_unit (function_definition (decl_specifiers (specifier_word int)) (declarator_function (declarator_attributed (attribute_list (attribute_gnu (attribute x))) (declarator_name fk)) (identifier_list (declarator_name a))) (declaration (decl_specifiers (specifier_word int)) (init_declarator (declarator_name a))) (compound_statement (return (identifier a)))))"));
        c_ast_test_expect(arguments, S8("typedef int (__attribute__((x)) F)(int); F g;"),
                          S8("(translation_unit (declaration (decl_specifiers (specifier_word typedef) (specifier_word int)) (init_declarator (declarator_function (declarator_attributed (attribute_list (attribute_gnu (attribute x))) (declarator_name F)) (parameter_list (parameter (decl_specifiers (specifier_word int))))))) (declaration (decl_specifiers (typedef_name F)) (init_declarator (declarator_name g))))"));
        c_ast_test_expect(arguments, S8("void g(int (__attribute__((x)) p));"),
                          S8("(translation_unit (declaration (decl_specifiers (specifier_word void)) (init_declarator (declarator_function (declarator_name g) (parameter_list (parameter (decl_specifiers (specifier_word int)) (declarator_attributed (attribute_list (attribute_gnu (attribute x))) (declarator_name p))))))))"));
        c_ast_test_expect(arguments, S8("void g(int (__attribute__((x)) p)[2]);"),
                          S8("(translation_unit (declaration (decl_specifiers (specifier_word void)) (init_declarator (declarator_function (declarator_name g) (parameter_list (parameter (decl_specifiers (specifier_word int)) (declarator_array (declarator_attributed (attribute_list (attribute_gnu (attribute x))) (declarator_name p)) (number 2))))))))"));
        c_ast_test_expect(arguments, S8("void g(int (__attribute__((x)) *));"),
                          S8("(translation_unit (declaration (decl_specifiers (specifier_word void)) (init_declarator (declarator_function (declarator_name g) (parameter_list (parameter (decl_specifiers (specifier_word int)) (declarator_pointer (attribute_list (attribute_gnu (attribute x))))))))))"));
        c_ast_test_expect(arguments, S8("void g(int (__attribute__((x)) [3]));"),
                          S8("(translation_unit (declaration (decl_specifiers (specifier_word void)) (init_declarator (declarator_function (declarator_name g) (parameter_list (parameter (decl_specifiers (specifier_word int)) (declarator_attributed (attribute_list (attribute_gnu (attribute x))) (declarator_array (number 3)))))))))"));
        c_ast_test_expect(arguments, S8("void g(int (__attribute__((x)) (void)));"),
                          S8("(translation_unit (declaration (decl_specifiers (specifier_word void)) (init_declarator (declarator_function (declarator_name g) (parameter_list (parameter (decl_specifiers (specifier_word int)) (declarator_attributed (attribute_list (attribute_gnu (attribute x))) (declarator_function (parameter_list (parameter (decl_specifiers (specifier_word void))))))))))))"));
        c_ast_test_expect(arguments, S8("int s = sizeof(int (__attribute__((x)) [3]));"),
                          S8("(translation_unit (declaration (decl_specifiers (specifier_word int)) (init_declarator (declarator_name s) (sizeof_type (type_name (decl_specifiers (specifier_word int)) (declarator_attributed (attribute_list (attribute_gnu (attribute x))) (declarator_array (number 3))))))))"));
        c_ast_test_expect(arguments, S8("int s = sizeof(int (__attribute__((x)) (void)));"),
                          S8("(translation_unit (declaration (decl_specifiers (specifier_word int)) (init_declarator (declarator_name s) (sizeof_type (type_name (decl_specifiers (specifier_word int)) (declarator_attributed (attribute_list (attribute_gnu (attribute x))) (declarator_function (parameter_list (parameter (decl_specifiers (specifier_word void)))))))))))"));
        c_ast_test_expect(arguments, S8("int s = sizeof(int (__attribute__((x)) *));"),
                          S8("(translation_unit (declaration (decl_specifiers (specifier_word int)) (init_declarator (declarator_name s) (sizeof_type (type_name (decl_specifiers (specifier_word int)) (declarator_pointer (attribute_list (attribute_gnu (attribute x)))))))))"));
        c_ast_test_expect(arguments, S8("struct S { int (__attribute__((x)) m); };"),
                          S8("(translation_unit (declaration (decl_specifiers (struct_specifier (tag_name S) (member_list (member_declaration (decl_specifiers (specifier_word int)) (member_declarator (declarator_attributed (attribute_list (attribute_gnu (attribute x))) (declarator_name m)))))))))"));
        c_ast_test_expect(arguments, S8("struct S { int (__attribute__((x)) m)[2]; };"),
                          S8("(translation_unit (declaration (decl_specifiers (struct_specifier (tag_name S) (member_list (member_declaration (decl_specifiers (specifier_word int)) (member_declarator (declarator_array (declarator_attributed (attribute_list (attribute_gnu (attribute x))) (declarator_name m)) (number 2)))))))))"));
        c_ast_test_expect(arguments, S8("struct S { int (__attribute__((x)) m) : 3; };"),
                          S8("(translation_unit (declaration (decl_specifiers (struct_specifier (tag_name S) (member_list (member_declaration (decl_specifiers (specifier_word int)) (member_declarator (declarator_attributed (attribute_list (attribute_gnu (attribute x))) (declarator_name m)) (number 3))))))))"));

        // parameters: abstract declarators, arrays and qualifiers
        c_ast_test_expect(arguments, S8("void f(int);"), S8("(translation_unit (declaration (decl_specifiers (specifier_word void)) (init_declarator (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word int))))))))"));
        c_ast_test_expect(arguments, S8("void f(int *);"), S8("(translation_unit (declaration (decl_specifiers (specifier_word void)) (init_declarator (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word int)) (declarator_pointer)))))))"));
        c_ast_test_expect(arguments, S8("void f(int []);"), S8("(translation_unit (declaration (decl_specifiers (specifier_word void)) (init_declarator (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word int)) (declarator_array)))))))"));
        c_ast_test_expect(arguments, S8("void f(int [3][4]);"), S8("(translation_unit (declaration (decl_specifiers (specifier_word void)) (init_declarator (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word int)) (declarator_array (declarator_array (number 3)) (number 4))))))))"));
        c_ast_test_expect(arguments, S8("void f(int *[3]);"), S8("(translation_unit (declaration (decl_specifiers (specifier_word void)) (init_declarator (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word int)) (declarator_pointer (declarator_array (number 3)))))))))"));
        c_ast_test_expect(arguments, S8("void f(int (*)[3]);"), S8("(translation_unit (declaration (decl_specifiers (specifier_word void)) (init_declarator (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word int)) (declarator_array (declarator_pointer) (number 3))))))))"));
        c_ast_test_expect(arguments, S8("void f(int (*)(char));"), S8("(translation_unit (declaration (decl_specifiers (specifier_word void)) (init_declarator (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word int)) (declarator_function (declarator_pointer) (parameter_list (parameter (decl_specifiers (specifier_word char)))))))))))"));
        c_ast_test_expect(arguments, S8("void f(int (int));"), S8("(translation_unit (declaration (decl_specifiers (specifier_word void)) (init_declarator (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word int)) (declarator_function (parameter_list (parameter (decl_specifiers (specifier_word int)))))))))))"));
        c_ast_test_expect(arguments, S8("void f(int ());"), S8("(translation_unit (declaration (decl_specifiers (specifier_word void)) (init_declarator (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word int)) (declarator_function (parameter_list))))))))"));
        c_ast_test_expect(arguments, S8("void f(int g(char));"), S8("(translation_unit (declaration (decl_specifiers (specifier_word void)) (init_declarator (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word int)) (declarator_function (declarator_name g) (parameter_list (parameter (decl_specifiers (specifier_word char)))))))))))"));
        c_ast_test_expect(arguments, S8("void f(int (a));"), S8("(translation_unit (declaration (decl_specifiers (specifier_word void)) (init_declarator (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word int)) (declarator_name a)))))))"));
        c_ast_test_expect(arguments, S8("void f(int a[static 3]);"), S8("(translation_unit (declaration (decl_specifiers (specifier_word void)) (init_declarator (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word int)) (declarator_array static (declarator_name a) (number 3))))))))"));
        c_ast_test_expect(arguments, S8("void f(int a[*]);"), S8("(translation_unit (declaration (decl_specifiers (specifier_word void)) (init_declarator (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word int)) (declarator_array * (declarator_name a))))))))"));
        c_ast_test_expect(arguments, S8("void f(int a[const 2]);"), S8("(translation_unit (declaration (decl_specifiers (specifier_word void)) (init_declarator (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word int)) (declarator_array const (declarator_name a) (number 2))))))))"));
        c_ast_test_expect(arguments, S8("void f(int a[static const 3]);"), S8("(translation_unit (declaration (decl_specifiers (specifier_word void)) (init_declarator (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word int)) (declarator_array const static (declarator_name a) (number 3))))))))"));
        c_ast_test_expect(arguments, S8("void f(int a[const volatile restrict n]);"), S8("(translation_unit (declaration (decl_specifiers (specifier_word void)) (init_declarator (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word int)) (declarator_array const volatile restrict (declarator_name a) (identifier n))))))))"));
        c_ast_test_expect(arguments, S8("void f(int n, int a[n]);"), S8("(translation_unit (declaration (decl_specifiers (specifier_word void)) (init_declarator (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word int)) (declarator_name n)) (parameter (decl_specifiers (specifier_word int)) (declarator_array (declarator_name a) (identifier n))))))))"));
        c_ast_test_expect(arguments, S8("void f(int a[*][3]);"), S8("(translation_unit (declaration (decl_specifiers (specifier_word void)) (init_declarator (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word int)) (declarator_array (declarator_array * (declarator_name a)) (number 3))))))))"));
        c_ast_test_expect(arguments, S8("void f(const char *restrict s);"), S8("(translation_unit (declaration (decl_specifiers (specifier_word void)) (init_declarator (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word const) (specifier_word char)) (declarator_pointer restrict (declarator_name s))))))))"));
        c_ast_test_expect(arguments, S8("void f(int a __attribute__((unused)));"), S8("(translation_unit (declaration (decl_specifiers (specifier_word void)) (init_declarator (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word int)) (declarator_name a) (attribute_list (attribute_gnu (attribute unused)))))))))"));
        c_ast_test_expect(arguments, S8("void f(int (*)[*]);"), S8("(translation_unit (declaration (decl_specifiers (specifier_word void)) (init_declarator (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word int)) (declarator_array * (declarator_pointer))))))))"));

        // declarators: abstract (type names) in sizeof and compound literals
        c_ast_test_expect(arguments, S8("void f(void) { sizeof(int (*)[3]); }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (sizeof_type (type_name (decl_specifiers (specifier_word int)) (declarator_array (declarator_pointer) (number 3))))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { sizeof(int (*)(void)); }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (sizeof_type (type_name (decl_specifiers (specifier_word int)) (declarator_function (declarator_pointer) (parameter_list (parameter (decl_specifiers (specifier_word void)))))))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { sizeof(char[2][3]); }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (sizeof_type (type_name (decl_specifiers (specifier_word char)) (declarator_array (declarator_array (number 2)) (number 3))))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { sizeof(int (*const)[3]); }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (sizeof_type (type_name (decl_specifiers (specifier_word int)) (declarator_array (declarator_pointer const) (number 3))))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { sizeof(int (*(*)(void))[2]); }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (sizeof_type (type_name (decl_specifiers (specifier_word int)) (declarator_array (declarator_pointer (declarator_function (declarator_pointer) (parameter_list (parameter (decl_specifiers (specifier_word void)))))) (number 2))))))))"));

        // K&R and function definitions
        c_ast_test_expect(arguments, S8("int main(void) { return 0; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word int)) (declarator_function (declarator_name main) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (return (number 0)))))"));
        c_ast_test_expect(arguments, S8("void f() {}"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list)) (compound_statement)))"));
        c_ast_test_expect(arguments, S8("static inline int g(int a, int b) { return a + b; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word static) (specifier_word inline) (specifier_word int)) (declarator_function (declarator_name g) (parameter_list (parameter (decl_specifiers (specifier_word int)) (declarator_name a)) (parameter (decl_specifiers (specifier_word int)) (declarator_name b)))) (compound_statement (return (add (identifier a) (identifier b))))))"));
        c_ast_test_expect(arguments, S8("int *f(void) { return 0; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word int)) (declarator_pointer (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void)))))) (compound_statement (return (number 0)))))"));
        c_ast_test_expect(arguments, S8("int (*f(void))(void) { return 0; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word int)) (declarator_function (declarator_pointer (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void)))))) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (return (number 0)))))"));
        c_ast_test_expect(arguments, S8("int f(a, b) int a; char *b; { return a; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word int)) (declarator_function (declarator_name f) (identifier_list (declarator_name a) (declarator_name b))) (declaration (decl_specifiers (specifier_word int)) (init_declarator (declarator_name a))) (declaration (decl_specifiers (specifier_word char)) (init_declarator (declarator_pointer (declarator_name b)))) (compound_statement (return (identifier a)))))"));
        c_ast_test_expect(arguments, S8("int f(a) { return a; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word int)) (declarator_function (declarator_name f) (identifier_list (declarator_name a))) (compound_statement (return (identifier a)))))"));
        c_ast_test_expect(arguments, S8("void f(a) register int a; { }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (identifier_list (declarator_name a))) (declaration (decl_specifiers (specifier_word register) (specifier_word int)) (init_declarator (declarator_name a))) (compound_statement)))"));
        c_ast_test_expect(arguments, S8("void f(a, b) int *a; double b[]; { }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (identifier_list (declarator_name a) (declarator_name b))) (declaration (decl_specifiers (specifier_word int)) (init_declarator (declarator_pointer (declarator_name a)))) (declaration (decl_specifiers (specifier_word double)) (init_declarator (declarator_array (declarator_name b)))) (compound_statement)))"));
        c_ast_test_expect(arguments, S8("int f(void) { return 1; } int g(void) { return 2; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word int)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (return (number 1)))) (function_definition (decl_specifiers (specifier_word int)) (declarator_function (declarator_name g) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (return (number 2)))))"));
        c_ast_test_expect(arguments, S8("int f(void); int f(void) { return 0; }"), S8("(translation_unit (declaration (decl_specifiers (specifier_word int)) (init_declarator (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))))) (function_definition (decl_specifiers (specifier_word int)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (return (number 0)))))"));

        // initializers
        c_ast_test_expect(arguments, S8("int a[] = {1, 2, 3};"), S8("(translation_unit (declaration (decl_specifiers (specifier_word int)) (init_declarator (declarator_array (declarator_name a)) (initializer_list (number 1) (number 2) (number 3)))))"));
        c_ast_test_expect(arguments, S8("int a[2] = {1,};"), S8("(translation_unit (declaration (decl_specifiers (specifier_word int)) (init_declarator (declarator_array (declarator_name a) (number 2)) (initializer_list (number 1)))))"));
        c_ast_test_expect(arguments, S8("int a[2] = {};"), S8("(translation_unit (declaration (decl_specifiers (specifier_word int)) (init_declarator (declarator_array (declarator_name a) (number 2)) (initializer_list))))"));
        c_ast_test_expect(arguments, S8("int x = { 1 };"), S8("(translation_unit (declaration (decl_specifiers (specifier_word int)) (init_declarator (declarator_name x) (initializer_list (number 1)))))"));
        c_ast_test_expect(arguments, S8("struct S s = { .a = 1 };"), S8("(translation_unit (declaration (decl_specifiers (struct_specifier (tag_name S))) (init_declarator (declarator_name s) (initializer_list (designation (designator_member a) (number 1))))))"));
        c_ast_test_expect(arguments, S8("int a[4] = { [2] = 3 };"), S8("(translation_unit (declaration (decl_specifiers (specifier_word int)) (init_declarator (declarator_array (declarator_name a) (number 4)) (initializer_list (designation (designator_index (number 2)) (number 3))))))"));
        c_ast_test_expect(arguments, S8("struct T t = { .a.b[1] = 2 };"), S8("(translation_unit (declaration (decl_specifiers (struct_specifier (tag_name T))) (init_declarator (declarator_name t) (initializer_list (designation (designator_member a) (designator_member b) (designator_index (number 1)) (number 2))))))"));
        c_ast_test_expect(arguments, S8("int a[8] = { [0 ... 3] = 1 };"), S8("(translation_unit (declaration (decl_specifiers (specifier_word int)) (init_declarator (declarator_array (declarator_name a) (number 8)) (initializer_list (designation (designator_range (number 0) (number 3)) (number 1))))))"));
        c_ast_test_expect(arguments, S8("struct S s = { 1, { 2, 3 }, 4 };"), S8("(translation_unit (declaration (decl_specifiers (struct_specifier (tag_name S))) (init_declarator (declarator_name s) (initializer_list (number 1) (initializer_list (number 2) (number 3)) (number 4)))))"));
        c_ast_test_expect(arguments, S8("int a[2][2] = { {1, 2}, {3, 4} };"), S8("(translation_unit (declaration (decl_specifiers (specifier_word int)) (init_declarator (declarator_array (declarator_array (declarator_name a) (number 2)) (number 2)) (initializer_list (initializer_list (number 1) (number 2)) (initializer_list (number 3) (number 4))))))"));
        c_ast_test_expect(arguments, S8("struct S s = { .a = { .b = 1 } };"), S8("(translation_unit (declaration (decl_specifiers (struct_specifier (tag_name S))) (init_declarator (declarator_name s) (initializer_list (designation (designator_member a) (initializer_list (designation (designator_member b) (number 1))))))))"));
        c_ast_test_expect(arguments, S8("int a[] = { [1] = 1, 2, [5] = 3 };"), S8("(translation_unit (declaration (decl_specifiers (specifier_word int)) (init_declarator (declarator_array (declarator_name a)) (initializer_list (designation (designator_index (number 1)) (number 1)) (number 2) (designation (designator_index (number 5)) (number 3))))))"));
        c_ast_test_expect(arguments, S8("struct S s = { 1, .b = 2 };"), S8("(translation_unit (declaration (decl_specifiers (struct_specifier (tag_name S))) (init_declarator (declarator_name s) (initializer_list (number 1) (designation (designator_member b) (number 2))))))"));
        c_ast_test_expect(arguments, S8("int a[2][3] = { [1][2] = 5 };"), S8("(translation_unit (declaration (decl_specifiers (specifier_word int)) (init_declarator (declarator_array (declarator_array (declarator_name a) (number 2)) (number 3)) (initializer_list (designation (designator_index (number 1)) (designator_index (number 2)) (number 5))))))"));
        c_ast_test_expect(arguments, S8("int x = (1, 2);"), S8("(translation_unit (declaration (decl_specifiers (specifier_word int)) (init_declarator (declarator_name x) (comma (number 1) (number 2)))))"));
        c_ast_test_expect(arguments, S8("char s[] = \"abc\";"), S8("(translation_unit (declaration (decl_specifiers (specifier_word char)) (init_declarator (declarator_array (declarator_name s)) (string \"abc\"))))"));
        c_ast_test_expect(arguments, S8("char *s = \"a\" \"b\";"), S8("(translation_unit (declaration (decl_specifiers (specifier_word char)) (init_declarator (declarator_pointer (declarator_name s)) (string \"a\" \"b\"))))"));

        // static assertions
        c_ast_test_expect(arguments, S8("_Static_assert(1, \"m\");"), S8("(translation_unit (static_assert (number 1) (string \"m\")))"));
        c_ast_test_expect(arguments, S8("_Static_assert(sizeof(int) == 4, \"int is 4 bytes\");"), S8("(translation_unit (static_assert (equal (sizeof_type (type_name (decl_specifiers (specifier_word int)))) (number 4)) (string \"int is 4 bytes\")))"));
        c_ast_test_expect(arguments, S8("_Static_assert(1, \"a\" \"b\");"), S8("(translation_unit (static_assert (number 1) (string \"a\" \"b\")))"));
        c_ast_test_expect(arguments, S8("void f(void) { _Static_assert(1, \"m\"); int x; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (static_assert (number 1) (string \"m\")) (declaration (decl_specifiers (specifier_word int)) (init_declarator (declarator_name x))))))"));
        c_ast_test_expect(arguments, S8("_Static_assert(1, \"m\");;"), S8("(translation_unit (static_assert (number 1) (string \"m\")) (empty_declaration))"));
        c_ast_oracle_expect_dialect(arguments, C_PREPROCESS_DIALECT_C23, S8("static_assert(1);"), S8("(translation_unit (static_assert (number 1)))"));
        c_ast_oracle_expect_dialect(arguments, C_PREPROCESS_DIALECT_C23, S8("static_assert(1, \"m\");"), S8("(translation_unit (static_assert (number 1) (string \"m\")))"));
        c_ast_oracle_expect_dialect(arguments, C_PREPROCESS_DIALECT_C23, S8("void f(void) { static_assert(a < b); }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (static_assert (less (identifier a) (identifier b))))))"));

        // statements: expression, null, compound
        c_ast_test_expect(arguments, S8("void f(void) { x; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (identifier x)))))"));
        c_ast_test_expect(arguments, S8("void f(void) { { } }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (compound_statement))))"));
        c_ast_test_expect(arguments, S8("void f(void) { { x; } }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (compound_statement (expression_statement (identifier x))))))"));

        // statements: if, switch, loops
        c_ast_test_expect(arguments, S8("void f(void) { if (a) b; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (if (identifier a) (expression_statement (identifier b))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { if (a) b; else c; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (if_else (identifier a) (expression_statement (identifier b)) (expression_statement (identifier c))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { if (a) if (b) x; else y; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (if (identifier a) (if_else (identifier b) (expression_statement (identifier x)) (expression_statement (identifier y)))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { if (a) if (b) x; else y; else z; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (if_else (identifier a) (if_else (identifier b) (expression_statement (identifier x)) (expression_statement (identifier y))) (expression_statement (identifier z))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { if (a) { if (b) x; } else y; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (if_else (identifier a) (compound_statement (if (identifier b) (expression_statement (identifier x)))) (expression_statement (identifier y))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { if (a) x; else if (b) y; else z; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (if_else (identifier a) (expression_statement (identifier x)) (if_else (identifier b) (expression_statement (identifier y)) (expression_statement (identifier z)))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { if (a, b) x; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (if (comma (identifier a) (identifier b)) (expression_statement (identifier x))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { while (a) b; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (while (identifier a) (expression_statement (identifier b))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { while (a); }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (while (identifier a) (null_statement)))))"));
        c_ast_test_expect(arguments, S8("void f(void) { do x; while (a); }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (do_while (expression_statement (identifier x)) (identifier a)))))"));
        c_ast_test_expect(arguments, S8("void f(void) { do x++; while (--n); }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (do_while (expression_statement (post_increment (identifier x))) (pre_decrement (identifier n))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { for (;;) ; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (for (null_statement)))))"));
        c_ast_test_expect(arguments, S8("void f(void) { for (i = 0; ; ) ; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (for (assign (identifier i) (number 0)) (null_statement)))))"));
        c_ast_test_expect(arguments, S8("void f(void) { for (;; i++) ; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (for (post_increment (identifier i)) (null_statement)))))"));
        c_ast_test_expect(arguments, S8("void f(void) { for (i = 0; i < 3; i++) x; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (for (assign (identifier i) (number 0)) (less (identifier i) (number 3)) (post_increment (identifier i)) (expression_statement (identifier x))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { for (int i = 0; i < n; i++) x; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (for (declaration (decl_specifiers (specifier_word int)) (init_declarator (declarator_name i) (number 0))) (less (identifier i) (identifier n)) (post_increment (identifier i)) (expression_statement (identifier x))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { for (int i = 0, j = 1; ;) ; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (for (declaration (decl_specifiers (specifier_word int)) (init_declarator (declarator_name i) (number 0)) (init_declarator (declarator_name j) (number 1))) (null_statement)))))"));
        c_ast_test_expect(arguments, S8("void f(void) { for (i = 0, j = 1; i < j; i++, j--) ; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (for (comma (assign (identifier i) (number 0)) (assign (identifier j) (number 1))) (less (identifier i) (identifier j)) (comma (post_increment (identifier i)) (post_decrement (identifier j))) (null_statement)))))"));
        c_ast_test_expect(arguments, S8("void f(void) { for (;;) for (;;) x; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (for (for (expression_statement (identifier x)))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { for (struct S *p = q; p; p = p->next) ; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (for (declaration (decl_specifiers (struct_specifier (tag_name S))) (init_declarator (declarator_pointer (declarator_name p)) (identifier q))) (identifier p) (assign (identifier p) (member_arrow next (identifier p))) (null_statement)))))"));

        // statements: switch, case, default
        c_ast_test_expect(arguments, S8("void f(void) { switch (a) { case 1: x; break; default: y; } }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (switch (identifier a) (compound_statement (case (number 1) (expression_statement (identifier x))) (break) (default (expression_statement (identifier y))))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { switch (a) { case 1: case 2: x; } }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (switch (identifier a) (compound_statement (case (number 1) (case (number 2) (expression_statement (identifier x)))))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { switch (a) { case 1: x; case 2: y; } }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (switch (identifier a) (compound_statement (case (number 1) (expression_statement (identifier x))) (case (number 2) (expression_statement (identifier y))))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { switch (a) { case 1 ... 5: x; } }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (switch (identifier a) (compound_statement (case_range (number 1) (number 5) (expression_statement (identifier x))))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { switch (a) { default: ; } }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (switch (identifier a) (compound_statement (default (null_statement)))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { switch (a) { case A + 1: ; } }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (switch (identifier a) (compound_statement (case (add (identifier A) (number 1)) (null_statement)))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { switch (a) { case 1 ? 2 : 3: ; } }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (switch (identifier a) (compound_statement (case (conditional (number 1) (number 2) (number 3)) (null_statement)))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { switch (a) { case 1: if (b) x; else y; } }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (switch (identifier a) (compound_statement (case (number 1) (if_else (identifier b) (expression_statement (identifier x)) (expression_statement (identifier y)))))))))"));

        // statements: jumps and labels
        c_ast_test_expect(arguments, S8("void f(void) { goto l; l: ; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (goto l) (labeled l (null_statement)))))"));
        c_ast_test_expect(arguments, S8("void f(void) { goto *p; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (goto_computed (identifier p)))))"));
        c_ast_test_expect(arguments, S8("void f(void) { goto *tab[i]; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (goto_computed (index (identifier tab) (identifier i))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { continue; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (continue))))"));
        c_ast_test_expect(arguments, S8("void f(void) { break; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (break))))"));
        c_ast_test_expect(arguments, S8("void f(void) { return; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (return))))"));
        c_ast_test_expect(arguments, S8("void f(void) { return a; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (return (identifier a)))))"));
        c_ast_test_expect(arguments, S8("void f(void) { return (struct S){1}; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (return (compound_literal (type_name (decl_specifiers (struct_specifier (tag_name S)))) (initializer_list (number 1)))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { l: x; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (labeled l (expression_statement (identifier x))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { l: __attribute__((unused)) ; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (labeled l (attribute_list (attribute_gnu (attribute unused))) (null_statement)))))"));
        c_ast_test_expect(arguments, S8("void f(void) { l: __attribute__((unused)) x; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (labeled l (attribute_list (attribute_gnu (attribute unused))) (expression_statement (identifier x))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { __attribute__((fallthrough)); }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (attribute_statement (attribute_list (attribute_gnu (attribute fallthrough)))))))"));

        // statements: block-scope declarations and mixed items
        c_ast_test_expect(arguments, S8("void f(void) { int a; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (declaration (decl_specifiers (specifier_word int)) (init_declarator (declarator_name a))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { int f(int); }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (declaration (decl_specifiers (specifier_word int)) (init_declarator (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word int))))))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { int (*fp)(int) = g; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (declaration (decl_specifiers (specifier_word int)) (init_declarator (declarator_function (declarator_pointer (declarator_name fp)) (parameter_list (parameter (decl_specifiers (specifier_word int))))) (identifier g))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { _Static_assert(1, \"m\"); }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (static_assert (number 1) (string \"m\")))))"));
        c_ast_test_expect(arguments, S8("void f(void) { void *p = &&l; l: ; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (declaration (decl_specifiers (specifier_word void)) (init_declarator (declarator_pointer (declarator_name p)) (label_address l))) (labeled l (null_statement)))))"));
        c_ast_test_expect(arguments, S8("void f(void) { __extension__ int a; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (declaration (decl_specifiers (specifier_word __extension__) (specifier_word int)) (init_declarator (declarator_name a))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { __auto_type a = 1; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (declaration (decl_specifiers (specifier_word __auto_type)) (init_declarator (declarator_name a) (number 1))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { int a = ({ int b = 1; b; }); }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (declaration (decl_specifiers (specifier_word int)) (init_declarator (declarator_name a) (statement_expression (compound_statement (declaration (decl_specifiers (specifier_word int)) (init_declarator (declarator_name b) (number 1))) (expression_statement (identifier b)))))))))"));

        // typedef names in declarations
        c_ast_test_expect(arguments, S8("typedef int T; T x;"), S8("(translation_unit (declaration (decl_specifiers (specifier_word typedef) (specifier_word int)) (init_declarator (declarator_name T))) (declaration (decl_specifiers (typedef_name T)) (init_declarator (declarator_name x))))"));
        c_ast_test_expect(arguments, S8("typedef int T; T *p;"), S8("(translation_unit (declaration (decl_specifiers (specifier_word typedef) (specifier_word int)) (init_declarator (declarator_name T))) (declaration (decl_specifiers (typedef_name T)) (init_declarator (declarator_pointer (declarator_name p)))))"));
        c_ast_test_expect(arguments, S8("typedef int T; T x, *y;"), S8("(translation_unit (declaration (decl_specifiers (specifier_word typedef) (specifier_word int)) (init_declarator (declarator_name T))) (declaration (decl_specifiers (typedef_name T)) (init_declarator (declarator_name x)) (init_declarator (declarator_pointer (declarator_name y)))))"));
        c_ast_test_expect(arguments, S8("typedef int T; const T x;"), S8("(translation_unit (declaration (decl_specifiers (specifier_word typedef) (specifier_word int)) (init_declarator (declarator_name T))) (declaration (decl_specifiers (specifier_word const) (typedef_name T)) (init_declarator (declarator_name x))))"));
        c_ast_test_expect(arguments, S8("typedef int T; T const x;"), S8("(translation_unit (declaration (decl_specifiers (specifier_word typedef) (specifier_word int)) (init_declarator (declarator_name T))) (declaration (decl_specifiers (typedef_name T) (specifier_word const)) (init_declarator (declarator_name x))))"));
        c_ast_test_expect(arguments, S8("typedef int T; static T x;"), S8("(translation_unit (declaration (decl_specifiers (specifier_word typedef) (specifier_word int)) (init_declarator (declarator_name T))) (declaration (decl_specifiers (specifier_word static) (typedef_name T)) (init_declarator (declarator_name x))))"));
        c_ast_test_expect(arguments, S8("typedef int T; extern T x;"), S8("(translation_unit (declaration (decl_specifiers (specifier_word typedef) (specifier_word int)) (init_declarator (declarator_name T))) (declaration (decl_specifiers (specifier_word extern) (typedef_name T)) (init_declarator (declarator_name x))))"));
        c_ast_test_expect(arguments, S8("typedef int T; T f(T a, T *b);"), S8("(translation_unit (declaration (decl_specifiers (specifier_word typedef) (specifier_word int)) (init_declarator (declarator_name T))) (declaration (decl_specifiers (typedef_name T)) (init_declarator (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (typedef_name T)) (declarator_name a)) (parameter (decl_specifiers (typedef_name T)) (declarator_pointer (declarator_name b))))))))"));
        c_ast_test_expect(arguments, S8("typedef int T; T f(void) { return 0; }"), S8("(translation_unit (declaration (decl_specifiers (specifier_word typedef) (specifier_word int)) (init_declarator (declarator_name T))) (function_definition (decl_specifiers (typedef_name T)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (return (number 0)))))"));
        c_ast_test_expect(arguments, S8("typedef int T; T;"), S8("(translation_unit (declaration (decl_specifiers (specifier_word typedef) (specifier_word int)) (init_declarator (declarator_name T))) (declaration (decl_specifiers (typedef_name T))))"));
        c_ast_test_expect(arguments, S8("typedef int T; T (x);"), S8("(translation_unit (declaration (decl_specifiers (specifier_word typedef) (specifier_word int)) (init_declarator (declarator_name T))) (declaration (decl_specifiers (typedef_name T)) (init_declarator (declarator_name x))))"));
        c_ast_test_expect(arguments, S8("typedef int T; T (*fp)(T);"), S8("(translation_unit (declaration (decl_specifiers (specifier_word typedef) (specifier_word int)) (init_declarator (declarator_name T))) (declaration (decl_specifiers (typedef_name T)) (init_declarator (declarator_function (declarator_pointer (declarator_name fp)) (parameter_list (parameter (decl_specifiers (typedef_name T))))))))"));
        c_ast_test_expect(arguments, S8("typedef int T; T a[3];"), S8("(translation_unit (declaration (decl_specifiers (specifier_word typedef) (specifier_word int)) (init_declarator (declarator_name T))) (declaration (decl_specifiers (typedef_name T)) (init_declarator (declarator_array (declarator_name a) (number 3)))))"));
        c_ast_test_expect(arguments, S8("typedef int T; T x = 1;"), S8("(translation_unit (declaration (decl_specifiers (specifier_word typedef) (specifier_word int)) (init_declarator (declarator_name T))) (declaration (decl_specifiers (typedef_name T)) (init_declarator (declarator_name x) (number 1))))"));
        c_ast_test_expect(arguments, S8("typedef int T; typedef int T; T x;"), S8("(translation_unit (declaration (decl_specifiers (specifier_word typedef) (specifier_word int)) (init_declarator (declarator_name T))) (declaration (decl_specifiers (specifier_word typedef) (specifier_word int)) (init_declarator (declarator_name T))) (declaration (decl_specifiers (typedef_name T)) (init_declarator (declarator_name x))))"));
        c_ast_test_expect(arguments, S8("typedef int T, *U; T a; U b;"), S8("(translation_unit (declaration (decl_specifiers (specifier_word typedef) (specifier_word int)) (init_declarator (declarator_name T)) (init_declarator (declarator_pointer (declarator_name U)))) (declaration (decl_specifiers (typedef_name T)) (init_declarator (declarator_name a))) (declaration (decl_specifiers (typedef_name U)) (init_declarator (declarator_name b))))"));
        c_ast_test_expect(arguments, S8("typedef int A[3]; A a;"), S8("(translation_unit (declaration (decl_specifiers (specifier_word typedef) (specifier_word int)) (init_declarator (declarator_array (declarator_name A) (number 3)))) (declaration (decl_specifiers (typedef_name A)) (init_declarator (declarator_name a))))"));
        c_ast_test_expect(arguments, S8("typedef int (*FP)(int); FP f;"), S8("(translation_unit (declaration (decl_specifiers (specifier_word typedef) (specifier_word int)) (init_declarator (declarator_function (declarator_pointer (declarator_name FP)) (parameter_list (parameter (decl_specifiers (specifier_word int))))))) (declaration (decl_specifiers (typedef_name FP)) (init_declarator (declarator_name f))))"));
        c_ast_test_expect(arguments, S8("typedef void (*H)(int); H table[3];"), S8("(translation_unit (declaration (decl_specifiers (specifier_word typedef) (specifier_word void)) (init_declarator (declarator_function (declarator_pointer (declarator_name H)) (parameter_list (parameter (decl_specifiers (specifier_word int))))))) (declaration (decl_specifiers (typedef_name H)) (init_declarator (declarator_array (declarator_name table) (number 3)))))"));
        c_ast_test_expect(arguments, S8("typedef void (*H)(int); H signal(int, H);"), S8("(translation_unit (declaration (decl_specifiers (specifier_word typedef) (specifier_word void)) (init_declarator (declarator_function (declarator_pointer (declarator_name H)) (parameter_list (parameter (decl_specifiers (specifier_word int))))))) (declaration (decl_specifiers (typedef_name H)) (init_declarator (declarator_function (declarator_name signal) (parameter_list (parameter (decl_specifiers (specifier_word int))) (parameter (decl_specifiers (typedef_name H))))))))"));
        c_ast_test_expect(arguments, S8("typedef struct S S; S *p;"), S8("(translation_unit (declaration (decl_specifiers (specifier_word typedef) (struct_specifier (tag_name S))) (init_declarator (declarator_name S))) (declaration (decl_specifiers (typedef_name S)) (init_declarator (declarator_pointer (declarator_name p)))))"));
        c_ast_test_expect(arguments, S8("typedef struct S { int a; } S; S s;"), S8("(translation_unit (declaration (decl_specifiers (specifier_word typedef) (struct_specifier (tag_name S) (member_list (member_declaration (decl_specifiers (specifier_word int)) (member_declarator (declarator_name a)))))) (init_declarator (declarator_name S))) (declaration (decl_specifiers (typedef_name S)) (init_declarator (declarator_name s))))"));
        c_ast_test_expect(arguments, S8("typedef int T __attribute__((aligned(8))); T x;"), S8("(translation_unit (declaration (decl_specifiers (specifier_word typedef) (specifier_word int)) (init_declarator (declarator_name T) (attribute_list (attribute_gnu (attribute aligned (number 8)))))) (declaration (decl_specifiers (typedef_name T)) (init_declarator (declarator_name x))))"));
        c_ast_test_expect(arguments, S8("typedef int T; void f(T t, T *u);"), S8("(translation_unit (declaration (decl_specifiers (specifier_word typedef) (specifier_word int)) (init_declarator (declarator_name T))) (declaration (decl_specifiers (specifier_word void)) (init_declarator (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (typedef_name T)) (declarator_name t)) (parameter (decl_specifiers (typedef_name T)) (declarator_pointer (declarator_name u))))))))"));
        c_ast_test_expect(arguments, S8("typedef int T; void f(T T);"), S8("(translation_unit (declaration (decl_specifiers (specifier_word typedef) (specifier_word int)) (init_declarator (declarator_name T))) (declaration (decl_specifiers (specifier_word void)) (init_declarator (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (typedef_name T)) (declarator_name T)))))))"));
        c_ast_test_expect(arguments, S8("typedef int T; struct T { int a; };"), S8("(translation_unit (declaration (decl_specifiers (specifier_word typedef) (specifier_word int)) (init_declarator (declarator_name T))) (declaration (decl_specifiers (struct_specifier (tag_name T) (member_list (member_declaration (decl_specifiers (specifier_word int)) (member_declarator (declarator_name a))))))))"));
        c_ast_test_expect(arguments, S8("typedef int T; struct S { int T; T x; };"), S8("(translation_unit (declaration (decl_specifiers (specifier_word typedef) (specifier_word int)) (init_declarator (declarator_name T))) (declaration (decl_specifiers (struct_specifier (tag_name S) (member_list (member_declaration (decl_specifiers (specifier_word int)) (member_declarator (declarator_name T))) (member_declaration (decl_specifiers (typedef_name T)) (member_declarator (declarator_name x))))))))"));
        c_ast_test_expect(arguments, S8("typedef int T; struct S { T a : 3; T *p; };"), S8("(translation_unit (declaration (decl_specifiers (specifier_word typedef) (specifier_word int)) (init_declarator (declarator_name T))) (declaration (decl_specifiers (struct_specifier (tag_name S) (member_list (member_declaration (decl_specifiers (typedef_name T)) (member_declarator (declarator_name a) (number 3))) (member_declaration (decl_specifiers (typedef_name T)) (member_declarator (declarator_pointer (declarator_name p)))))))))"));
        c_ast_test_expect(arguments, S8("typedef int T; T x __attribute__((unused));"), S8("(translation_unit (declaration (decl_specifiers (specifier_word typedef) (specifier_word int)) (init_declarator (declarator_name T))) (declaration (decl_specifiers (typedef_name T)) (init_declarator (declarator_name x) (attribute_list (attribute_gnu (attribute unused))))))"));

        // typedef names and scopes
        c_ast_test_expect(arguments, S8("typedef int T; void f(void) { T * p; }"), S8("(translation_unit (declaration (decl_specifiers (specifier_word typedef) (specifier_word int)) (init_declarator (declarator_name T))) (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (declaration (decl_specifiers (typedef_name T)) (init_declarator (declarator_pointer (declarator_name p)))))))"));
        c_ast_test_expect(arguments, S8("typedef int T; void f(void) { T x; }"), S8("(translation_unit (declaration (decl_specifiers (specifier_word typedef) (specifier_word int)) (init_declarator (declarator_name T))) (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (declaration (decl_specifiers (typedef_name T)) (init_declarator (declarator_name x))))))"));
        c_ast_test_expect(arguments, S8("typedef int T; void f(void) { int T = 2; T * 3; }"), S8("(translation_unit (declaration (decl_specifiers (specifier_word typedef) (specifier_word int)) (init_declarator (declarator_name T))) (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (declaration (decl_specifiers (specifier_word int)) (init_declarator (declarator_name T) (number 2))) (expression_statement (multiply (identifier T) (number 3))))))"));
        c_ast_test_expect(arguments, S8("typedef int T; void f(void) { { int T; } T x; }"), S8("(translation_unit (declaration (decl_specifiers (specifier_word typedef) (specifier_word int)) (init_declarator (declarator_name T))) (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (compound_statement (declaration (decl_specifiers (specifier_word int)) (init_declarator (declarator_name T)))) (declaration (decl_specifiers (typedef_name T)) (init_declarator (declarator_name x))))))"));
        c_ast_test_expect(arguments, S8("typedef int T; void f(void) { { T x; } T y; }"), S8("(translation_unit (declaration (decl_specifiers (specifier_word typedef) (specifier_word int)) (init_declarator (declarator_name T))) (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (compound_statement (declaration (decl_specifiers (typedef_name T)) (init_declarator (declarator_name x)))) (declaration (decl_specifiers (typedef_name T)) (init_declarator (declarator_name y))))))"));
        c_ast_test_expect(arguments, S8("typedef int T; void f(void) { int T = sizeof(T); }"), S8("(translation_unit (declaration (decl_specifiers (specifier_word typedef) (specifier_word int)) (init_declarator (declarator_name T))) (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (declaration (decl_specifiers (specifier_word int)) (init_declarator (declarator_name T) (sizeof_expression (identifier T)))))))"));
        c_ast_test_expect(arguments, S8("typedef int T; void f(void) { int x = sizeof(T); }"), S8("(translation_unit (declaration (decl_specifiers (specifier_word typedef) (specifier_word int)) (init_declarator (declarator_name T))) (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (declaration (decl_specifiers (specifier_word int)) (init_declarator (declarator_name x) (sizeof_type (type_name (decl_specifiers (typedef_name T)))))))))"));
        c_ast_test_expect(arguments, S8("typedef int T; void f(void) { int (*T)[sizeof(T)]; }"), S8("(translation_unit (declaration (decl_specifiers (specifier_word typedef) (specifier_word int)) (init_declarator (declarator_name T))) (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (declaration (decl_specifiers (specifier_word int)) (init_declarator (declarator_array (declarator_pointer (declarator_name T)) (sizeof_type (type_name (decl_specifiers (typedef_name T))))))))))"));
        c_ast_test_expect(arguments, S8("typedef int T; void f(void) { unsigned T; T * 2; }"), S8("(translation_unit (declaration (decl_specifiers (specifier_word typedef) (specifier_word int)) (init_declarator (declarator_name T))) (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (declaration (decl_specifiers (specifier_word unsigned)) (init_declarator (declarator_name T))) (expression_statement (multiply (identifier T) (number 2))))))"));
        c_ast_test_expect(arguments, S8("typedef int T; void f(void) { T T = 1; }"), S8("(translation_unit (declaration (decl_specifiers (specifier_word typedef) (specifier_word int)) (init_declarator (declarator_name T))) (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (declaration (decl_specifiers (typedef_name T)) (init_declarator (declarator_name T) (number 1))))))"));
        c_ast_test_expect(arguments, S8("typedef int T; void f(void) { T: ; T x; }"), S8("(translation_unit (declaration (decl_specifiers (specifier_word typedef) (specifier_word int)) (init_declarator (declarator_name T))) (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (labeled T (null_statement)) (declaration (decl_specifiers (typedef_name T)) (init_declarator (declarator_name x))))))"));
        c_ast_test_expect(arguments, S8("typedef int T; void f(void) { goto T; T: ; }"), S8("(translation_unit (declaration (decl_specifiers (specifier_word typedef) (specifier_word int)) (init_declarator (declarator_name T))) (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (goto T) (labeled T (null_statement)))))"));
        c_ast_test_expect(arguments, S8("typedef int T; void f(void) { for (int T = 0; T < 3; T++) ; T x; }"), S8("(translation_unit (declaration (decl_specifiers (specifier_word typedef) (specifier_word int)) (init_declarator (declarator_name T))) (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (for (declaration (decl_specifiers (specifier_word int)) (init_declarator (declarator_name T) (number 0))) (less (identifier T) (number 3)) (post_increment (identifier T)) (null_statement)) (declaration (decl_specifiers (typedef_name T)) (init_declarator (declarator_name x))))))"));
        c_ast_test_expect(arguments, S8("typedef int T; void f(void) { for (T i = 0; i < 3; i++) ; }"), S8("(translation_unit (declaration (decl_specifiers (specifier_word typedef) (specifier_word int)) (init_declarator (declarator_name T))) (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (for (declaration (decl_specifiers (typedef_name T)) (init_declarator (declarator_name i) (number 0))) (less (identifier i) (number 3)) (post_increment (identifier i)) (null_statement)))))"));
        c_ast_test_expect(arguments, S8("typedef int T; void f(void) { for (T T = 0; T < 3; T++) ; T x; }"), S8("(translation_unit (declaration (decl_specifiers (specifier_word typedef) (specifier_word int)) (init_declarator (declarator_name T))) (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (for (declaration (decl_specifiers (typedef_name T)) (init_declarator (declarator_name T) (number 0))) (less (identifier T) (number 3)) (post_increment (identifier T)) (null_statement)) (declaration (decl_specifiers (typedef_name T)) (init_declarator (declarator_name x))))))"));
        c_ast_test_expect(arguments, S8("typedef int T; void f(void) { T x; { int T; T = 1; } T y; }"), S8("(translation_unit (declaration (decl_specifiers (specifier_word typedef) (specifier_word int)) (init_declarator (declarator_name T))) (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (declaration (decl_specifiers (typedef_name T)) (init_declarator (declarator_name x))) (compound_statement (declaration (decl_specifiers (specifier_word int)) (init_declarator (declarator_name T))) (expression_statement (assign (identifier T) (number 1)))) (declaration (decl_specifiers (typedef_name T)) (init_declarator (declarator_name y))))))"));
        c_ast_test_expect(arguments, S8("typedef int T; void f(void) { typedef int U; { T x; U y; } }"), S8("(translation_unit (declaration (decl_specifiers (specifier_word typedef) (specifier_word int)) (init_declarator (declarator_name T))) (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (declaration (decl_specifiers (specifier_word typedef) (specifier_word int)) (init_declarator (declarator_name U))) (compound_statement (declaration (decl_specifiers (typedef_name T)) (init_declarator (declarator_name x))) (declaration (decl_specifiers (typedef_name U)) (init_declarator (declarator_name y)))))))"));
        c_ast_test_expect(arguments, S8("typedef int T; void f(void) { enum { T }; T * 2; }"), S8("(translation_unit (declaration (decl_specifiers (specifier_word typedef) (specifier_word int)) (init_declarator (declarator_name T))) (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (declaration (decl_specifiers (enum_specifier (enumerator_list (enumerator T))))) (expression_statement (multiply (identifier T) (number 2))))))"));
        c_ast_test_expect(arguments, S8("typedef int T; void f(void) { T x; T (y); T *z; }"), S8("(translation_unit (declaration (decl_specifiers (specifier_word typedef) (specifier_word int)) (init_declarator (declarator_name T))) (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (declaration (decl_specifiers (typedef_name T)) (init_declarator (declarator_name x))) (declaration (decl_specifiers (typedef_name T)) (init_declarator (declarator_name y))) (declaration (decl_specifiers (typedef_name T)) (init_declarator (declarator_pointer (declarator_name z)))))))"));
        c_ast_test_expect(arguments, S8("typedef int T; void f(void) { int T; (T)+1; }"), S8("(translation_unit (declaration (decl_specifiers (specifier_word typedef) (specifier_word int)) (init_declarator (declarator_name T))) (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (declaration (decl_specifiers (specifier_word int)) (init_declarator (declarator_name T))) (expression_statement (add (identifier T) (number 1))))))"));
        c_ast_test_expect(arguments, S8("typedef int T; void f(void) { int T; (T)(1); }"), S8("(translation_unit (declaration (decl_specifiers (specifier_word typedef) (specifier_word int)) (init_declarator (declarator_name T))) (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (declaration (decl_specifiers (specifier_word int)) (init_declarator (declarator_name T))) (expression_statement (call (identifier T) (number 1))))))"));
        c_ast_test_expect(arguments, S8("typedef int T; void f(void) { a.T; }"), S8("(translation_unit (declaration (decl_specifiers (specifier_word typedef) (specifier_word int)) (init_declarator (declarator_name T))) (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (member T (identifier a))))))"));
        c_ast_test_expect(arguments, S8("typedef int T; void f(void) { a->T; }"), S8("(translation_unit (declaration (decl_specifiers (specifier_word typedef) (specifier_word int)) (init_declarator (declarator_name T))) (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (member_arrow T (identifier a))))))"));
        c_ast_test_expect(arguments, S8("typedef int T; void f(void) { T; }"), S8("(translation_unit (declaration (decl_specifiers (specifier_word typedef) (specifier_word int)) (init_declarator (declarator_name T))) (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (declaration (decl_specifiers (typedef_name T))))))"));
        c_ast_test_expect(arguments, S8("typedef int T; void f(void) { static T x; }"), S8("(translation_unit (declaration (decl_specifiers (specifier_word typedef) (specifier_word int)) (init_declarator (declarator_name T))) (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (declaration (decl_specifiers (specifier_word static) (typedef_name T)) (init_declarator (declarator_name x))))))"));
        c_ast_test_expect(arguments, S8("typedef int T; void f(void) { T x = (T)1; }"), S8("(translation_unit (declaration (decl_specifiers (specifier_word typedef) (specifier_word int)) (init_declarator (declarator_name T))) (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (declaration (decl_specifiers (typedef_name T)) (init_declarator (declarator_name x) (cast (type_name (decl_specifiers (typedef_name T))) (number 1)))))))"));
        c_ast_test_expect(arguments, S8("typedef int T; void f(void) { struct S { int T; } s; T x; }"), S8("(translation_unit (declaration (decl_specifiers (specifier_word typedef) (specifier_word int)) (init_declarator (declarator_name T))) (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (declaration (decl_specifiers (struct_specifier (tag_name S) (member_list (member_declaration (decl_specifiers (specifier_word int)) (member_declarator (declarator_name T)))))) (init_declarator (declarator_name s))) (declaration (decl_specifiers (typedef_name T)) (init_declarator (declarator_name x))))))"));
        c_ast_test_expect(arguments, S8("typedef int T; void f(void) { struct T { int a; } s; T x; }"), S8("(translation_unit (declaration (decl_specifiers (specifier_word typedef) (specifier_word int)) (init_declarator (declarator_name T))) (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (declaration (decl_specifiers (struct_specifier (tag_name T) (member_list (member_declaration (decl_specifiers (specifier_word int)) (member_declarator (declarator_name a)))))) (init_declarator (declarator_name s))) (declaration (decl_specifiers (typedef_name T)) (init_declarator (declarator_name x))))))"));
        c_ast_test_expect(arguments, S8("typedef int T; void f(void) { { enum { T }; T * 2; } T x; }"), S8("(translation_unit (declaration (decl_specifiers (specifier_word typedef) (specifier_word int)) (init_declarator (declarator_name T))) (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (compound_statement (declaration (decl_specifiers (enum_specifier (enumerator_list (enumerator T))))) (expression_statement (multiply (identifier T) (number 2)))) (declaration (decl_specifiers (typedef_name T)) (init_declarator (declarator_name x))))))"));
        c_ast_test_expect(arguments, S8("typedef int T; void f(void) { if (a) { int T; } T x; }"), S8("(translation_unit (declaration (decl_specifiers (specifier_word typedef) (specifier_word int)) (init_declarator (declarator_name T))) (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (if (identifier a) (compound_statement (declaration (decl_specifiers (specifier_word int)) (init_declarator (declarator_name T))))) (declaration (decl_specifiers (typedef_name T)) (init_declarator (declarator_name x))))))"));
        c_ast_test_expect(arguments, S8("typedef int T; void f(void) { while (a) { int T; } T x; }"), S8("(translation_unit (declaration (decl_specifiers (specifier_word typedef) (specifier_word int)) (init_declarator (declarator_name T))) (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (while (identifier a) (compound_statement (declaration (decl_specifiers (specifier_word int)) (init_declarator (declarator_name T))))) (declaration (decl_specifiers (typedef_name T)) (init_declarator (declarator_name x))))))"));
        c_ast_test_expect(arguments, S8("typedef int T; void f(void) { switch (a) { case 1: { int T; } } T x; }"), S8("(translation_unit (declaration (decl_specifiers (specifier_word typedef) (specifier_word int)) (init_declarator (declarator_name T))) (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (switch (identifier a) (compound_statement (case (number 1) (compound_statement (declaration (decl_specifiers (specifier_word int)) (init_declarator (declarator_name T))))))) (declaration (decl_specifiers (typedef_name T)) (init_declarator (declarator_name x))))))"));
        c_ast_test_expect(arguments, S8("typedef int T; void f(void) { int x = ({ int T = 1; T; }); T y; }"), S8("(translation_unit (declaration (decl_specifiers (specifier_word typedef) (specifier_word int)) (init_declarator (declarator_name T))) (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (declaration (decl_specifiers (specifier_word int)) (init_declarator (declarator_name x) (statement_expression (compound_statement (declaration (decl_specifiers (specifier_word int)) (init_declarator (declarator_name T) (number 1))) (expression_statement (identifier T)))))) (declaration (decl_specifiers (typedef_name T)) (init_declarator (declarator_name y))))))"));

        // typedef names: shadowing by parameters and enumerators at file scope
        c_ast_test_expect(arguments, S8("typedef int T; void f(int T) { T * 2; }"), S8("(translation_unit (declaration (decl_specifiers (specifier_word typedef) (specifier_word int)) (init_declarator (declarator_name T))) (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word int)) (declarator_name T)))) (compound_statement (expression_statement (multiply (identifier T) (number 2))))))"));
        c_ast_test_expect(arguments, S8("typedef int T; void f(int T) { } T x;"), S8("(translation_unit (declaration (decl_specifiers (specifier_word typedef) (specifier_word int)) (init_declarator (declarator_name T))) (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word int)) (declarator_name T)))) (compound_statement)) (declaration (decl_specifiers (typedef_name T)) (init_declarator (declarator_name x))))"));
        c_ast_test_expect(arguments, S8("typedef int T; void f(int T); T x;"), S8("(translation_unit (declaration (decl_specifiers (specifier_word typedef) (specifier_word int)) (init_declarator (declarator_name T))) (declaration (decl_specifiers (specifier_word void)) (init_declarator (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word int)) (declarator_name T)))))) (declaration (decl_specifiers (typedef_name T)) (init_declarator (declarator_name x))))"));
        c_ast_test_expect(arguments, S8("typedef int T; void f(int T, int a[sizeof(T)]);"), S8("(translation_unit (declaration (decl_specifiers (specifier_word typedef) (specifier_word int)) (init_declarator (declarator_name T))) (declaration (decl_specifiers (specifier_word void)) (init_declarator (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word int)) (declarator_name T)) (parameter (decl_specifiers (specifier_word int)) (declarator_array (declarator_name a) (sizeof_expression (identifier T)))))))))"));
        c_ast_test_expect(arguments, S8("typedef int T; void f(T n, int a[sizeof(T)]);"), S8("(translation_unit (declaration (decl_specifiers (specifier_word typedef) (specifier_word int)) (init_declarator (declarator_name T))) (declaration (decl_specifiers (specifier_word void)) (init_declarator (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (typedef_name T)) (declarator_name n)) (parameter (decl_specifiers (specifier_word int)) (declarator_array (declarator_name a) (sizeof_type (type_name (decl_specifiers (typedef_name T)))))))))))"));
        c_ast_test_expect(arguments, S8("typedef int T; int (*f(int T))(void) { T * 2; }"), S8("(translation_unit (declaration (decl_specifiers (specifier_word typedef) (specifier_word int)) (init_declarator (declarator_name T))) (function_definition (decl_specifiers (specifier_word int)) (declarator_function (declarator_pointer (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word int)) (declarator_name T))))) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (multiply (identifier T) (number 2))))))"));
        c_ast_test_expect(arguments, S8("typedef int T; void (*f(int a))(T T) { T * p; }"), S8("(translation_unit (declaration (decl_specifiers (specifier_word typedef) (specifier_word int)) (init_declarator (declarator_name T))) (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_pointer (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word int)) (declarator_name a))))) (parameter_list (parameter (decl_specifiers (typedef_name T)) (declarator_name T)))) (compound_statement (declaration (decl_specifiers (typedef_name T)) (init_declarator (declarator_pointer (declarator_name p)))))))"));
        c_ast_test_expect(arguments, S8("typedef int T; enum { T }; int x = T;"), S8("(translation_unit (declaration (decl_specifiers (specifier_word typedef) (specifier_word int)) (init_declarator (declarator_name T))) (declaration (decl_specifiers (enum_specifier (enumerator_list (enumerator T))))) (declaration (decl_specifiers (specifier_word int)) (init_declarator (declarator_name x) (identifier T))))"));
        c_ast_test_expect(arguments, S8("typedef int T; unsigned T;"), S8("(translation_unit (declaration (decl_specifiers (specifier_word typedef) (specifier_word int)) (init_declarator (declarator_name T))) (declaration (decl_specifiers (specifier_word unsigned)) (init_declarator (declarator_name T))))"));
        c_ast_test_expect(arguments, S8("typedef int T; void f(void) { typedef int U; U u; } void g(void) { T t; }"), S8("(translation_unit (declaration (decl_specifiers (specifier_word typedef) (specifier_word int)) (init_declarator (declarator_name T))) (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (declaration (decl_specifiers (specifier_word typedef) (specifier_word int)) (init_declarator (declarator_name U))) (declaration (decl_specifiers (typedef_name U)) (init_declarator (declarator_name u))))) (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name g) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (declaration (decl_specifiers (typedef_name T)) (init_declarator (declarator_name t))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { typedef int T; T x; } void g(void) { T * y; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (declaration (decl_specifiers (specifier_word typedef) (specifier_word int)) (init_declarator (declarator_name T))) (declaration (decl_specifiers (typedef_name T)) (init_declarator (declarator_name x))))) (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name g) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (multiply (identifier T) (identifier y))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { { typedef int T; } T * y; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (compound_statement (declaration (decl_specifiers (specifier_word typedef) (specifier_word int)) (init_declarator (declarator_name T)))) (expression_statement (multiply (identifier T) (identifier y))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { for (;;) { typedef int T; T x; } T * y; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (for (compound_statement (declaration (decl_specifiers (specifier_word typedef) (specifier_word int)) (init_declarator (declarator_name T))) (declaration (decl_specifiers (typedef_name T)) (init_declarator (declarator_name x))))) (expression_statement (multiply (identifier T) (identifier y))))))"));
        c_ast_test_expect(arguments, S8("typedef int T; void f(void) { typedef long T; T x; }"), S8("(translation_unit (declaration (decl_specifiers (specifier_word typedef) (specifier_word int)) (init_declarator (declarator_name T))) (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (declaration (decl_specifiers (specifier_word typedef) (specifier_word long)) (init_declarator (declarator_name T))) (declaration (decl_specifiers (typedef_name T)) (init_declarator (declarator_name x))))))"));
        c_ast_test_expect(arguments, S8("typedef int T; void f(void) { extern int T; T * 2; }"), S8("(translation_unit (declaration (decl_specifiers (specifier_word typedef) (specifier_word int)) (init_declarator (declarator_name T))) (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (declaration (decl_specifiers (specifier_word extern) (specifier_word int)) (init_declarator (declarator_name T))) (expression_statement (multiply (identifier T) (number 2))))))"));

        // typedef names in casts, sizeof, calls and type-name contexts
        c_ast_test_expect(arguments, S8("typedef int T; void f(void) { (T)x; }"), S8("(translation_unit (declaration (decl_specifiers (specifier_word typedef) (specifier_word int)) (init_declarator (declarator_name T))) (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (cast (type_name (decl_specifiers (typedef_name T))) (identifier x))))))"));
        c_ast_test_expect(arguments, S8("typedef int T; void f(void) { (T)(x); }"), S8("(translation_unit (declaration (decl_specifiers (specifier_word typedef) (specifier_word int)) (init_declarator (declarator_name T))) (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (cast (type_name (decl_specifiers (typedef_name T))) (identifier x))))))"));
        c_ast_test_expect(arguments, S8("typedef int T; void f(void) { (T)-x; }"), S8("(translation_unit (declaration (decl_specifiers (specifier_word typedef) (specifier_word int)) (init_declarator (declarator_name T))) (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (cast (type_name (decl_specifiers (typedef_name T))) (negate (identifier x)))))))"));
        c_ast_test_expect(arguments, S8("typedef int T; void f(void) { (T)+x; }"), S8("(translation_unit (declaration (decl_specifiers (specifier_word typedef) (specifier_word int)) (init_declarator (declarator_name T))) (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (cast (type_name (decl_specifiers (typedef_name T))) (plus (identifier x)))))))"));
        c_ast_test_expect(arguments, S8("typedef int T; void f(void) { (T)*p; }"), S8("(translation_unit (declaration (decl_specifiers (specifier_word typedef) (specifier_word int)) (init_declarator (declarator_name T))) (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (cast (type_name (decl_specifiers (typedef_name T))) (dereference (identifier p)))))))"));
        c_ast_test_expect(arguments, S8("typedef int T; void f(void) { (T)&x; }"), S8("(translation_unit (declaration (decl_specifiers (specifier_word typedef) (specifier_word int)) (init_declarator (declarator_name T))) (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (cast (type_name (decl_specifiers (typedef_name T))) (address (identifier x)))))))"));
        c_ast_test_expect(arguments, S8("typedef int T; void f(void) { (T)x + y; }"), S8("(translation_unit (declaration (decl_specifiers (specifier_word typedef) (specifier_word int)) (init_declarator (declarator_name T))) (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (add (cast (type_name (decl_specifiers (typedef_name T))) (identifier x)) (identifier y))))))"));
        c_ast_test_expect(arguments, S8("typedef int T; void f(void) { (T *)p; }"), S8("(translation_unit (declaration (decl_specifiers (specifier_word typedef) (specifier_word int)) (init_declarator (declarator_name T))) (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (cast (type_name (decl_specifiers (typedef_name T)) (declarator_pointer)) (identifier p))))))"));
        c_ast_test_expect(arguments, S8("typedef int T; void f(void) { (T){1}; }"), S8("(translation_unit (declaration (decl_specifiers (specifier_word typedef) (specifier_word int)) (init_declarator (declarator_name T))) (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (compound_literal (type_name (decl_specifiers (typedef_name T))) (initializer_list (number 1)))))))"));
        c_ast_test_expect(arguments, S8("typedef int T; void f(void) { (T[]){1, 2}[0]; }"), S8("(translation_unit (declaration (decl_specifiers (specifier_word typedef) (specifier_word int)) (init_declarator (declarator_name T))) (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (index (compound_literal (type_name (decl_specifiers (typedef_name T)) (declarator_array)) (initializer_list (number 1) (number 2))) (number 0))))))"));
        c_ast_test_expect(arguments, S8("typedef int T; void f(void) { sizeof(T) * 2; }"), S8("(translation_unit (declaration (decl_specifiers (specifier_word typedef) (specifier_word int)) (init_declarator (declarator_name T))) (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (multiply (sizeof_type (type_name (decl_specifiers (typedef_name T)))) (number 2))))))"));
        c_ast_test_expect(arguments, S8("typedef int T; void f(void) { sizeof(T *); }"), S8("(translation_unit (declaration (decl_specifiers (specifier_word typedef) (specifier_word int)) (init_declarator (declarator_name T))) (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (sizeof_type (type_name (decl_specifiers (typedef_name T)) (declarator_pointer)))))))"));
        c_ast_test_expect(arguments, S8("typedef int T; void f(void) { sizeof (T){1}; }"), S8("(translation_unit (declaration (decl_specifiers (specifier_word typedef) (specifier_word int)) (init_declarator (declarator_name T))) (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (sizeof_expression (compound_literal (type_name (decl_specifiers (typedef_name T))) (initializer_list (number 1))))))))"));
        c_ast_test_expect(arguments, S8("typedef int T; void f(void) { __builtin_va_arg(ap, T); }"), S8("(translation_unit (declaration (decl_specifiers (specifier_word typedef) (specifier_word int)) (init_declarator (declarator_name T))) (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (call (identifier __builtin_va_arg) (identifier ap) (type_name (decl_specifiers (typedef_name T))))))))"));
        c_ast_test_expect(arguments, S8("typedef int T; void f(void) { __builtin_types_compatible_p(T, int); }"), S8("(translation_unit (declaration (decl_specifiers (specifier_word typedef) (specifier_word int)) (init_declarator (declarator_name T))) (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (call (identifier __builtin_types_compatible_p) (type_name (decl_specifiers (typedef_name T))) (type_name (decl_specifiers (specifier_word int))))))))"));
        c_ast_test_expect(arguments, S8("typedef int T; void f(void) { __builtin_types_compatible_p(T *, T); }"), S8("(translation_unit (declaration (decl_specifiers (specifier_word typedef) (specifier_word int)) (init_declarator (declarator_name T))) (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (call (identifier __builtin_types_compatible_p) (type_name (decl_specifiers (typedef_name T)) (declarator_pointer)) (type_name (decl_specifiers (typedef_name T))))))))"));
        c_ast_test_expect(arguments, S8("typedef int T; void f(void) { __builtin_offsetof(T, a); }"), S8("(translation_unit (declaration (decl_specifiers (specifier_word typedef) (specifier_word int)) (init_declarator (declarator_name T))) (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (call (identifier __builtin_offsetof) (type_name (decl_specifiers (typedef_name T))) (identifier a))))))"));
        c_ast_test_expect(arguments, S8("typedef int T; void f(void) { f(T, 1); }"), S8("(translation_unit (declaration (decl_specifiers (specifier_word typedef) (specifier_word int)) (init_declarator (declarator_name T))) (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (call (identifier f) (type_name (decl_specifiers (typedef_name T))) (number 1))))))"));
        c_ast_test_expect(arguments, S8("typedef int T; void f(void) { f(1, T); }"), S8("(translation_unit (declaration (decl_specifiers (specifier_word typedef) (specifier_word int)) (init_declarator (declarator_name T))) (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (call (identifier f) (number 1) (type_name (decl_specifiers (typedef_name T))))))))"));
        c_ast_test_expect(arguments, S8("typedef int T; void f(void) { x = _Generic(y, T: 1, default: 2); }"), S8("(translation_unit (declaration (decl_specifiers (specifier_word typedef) (specifier_word int)) (init_declarator (declarator_name T))) (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (assign (identifier x) (generic_selection (identifier y) (generic_association (type_name (decl_specifiers (typedef_name T))) (number 1)) (generic_default (number 2))))))))"));
        c_ast_test_expect(arguments, S8("typedef int m; void f(void) { __builtin_offsetof(struct S, m); }"), S8("(translation_unit (declaration (decl_specifiers (specifier_word typedef) (specifier_word int)) (init_declarator (declarator_name m))) (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (call (identifier __builtin_offsetof) (type_name (decl_specifiers (struct_specifier (tag_name S)))) (identifier m))))))"));
        c_ast_test_expect(arguments, S8("typedef int m; void f(void) { __builtin_offsetof(struct S, a.m); }"), S8("(translation_unit (declaration (decl_specifiers (specifier_word typedef) (specifier_word int)) (init_declarator (declarator_name m))) (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (call (identifier __builtin_offsetof) (type_name (decl_specifiers (struct_specifier (tag_name S)))) (member m (identifier a)))))))"));
        c_ast_test_expect(arguments, S8("typedef int m; void f(void) { __builtin_offsetof(struct S, m[1]); }"), S8("(translation_unit (declaration (decl_specifiers (specifier_word typedef) (specifier_word int)) (init_declarator (declarator_name m))) (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (call (identifier __builtin_offsetof) (type_name (decl_specifiers (struct_specifier (tag_name S)))) (index (identifier m) (number 1)))))))"));
        c_ast_test_expect(arguments, S8("typedef int T; void f(void) { typeof(T) x; }"), S8("(translation_unit (declaration (decl_specifiers (specifier_word typedef) (specifier_word int)) (init_declarator (declarator_name T))) (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (declaration (decl_specifiers (typeof (type_name (decl_specifiers (typedef_name T))))) (init_declarator (declarator_name x))))))"));
        c_ast_test_expect(arguments, S8("typedef int T; void f(void) { _Atomic(T) x; }"), S8("(translation_unit (declaration (decl_specifiers (specifier_word typedef) (specifier_word int)) (init_declarator (declarator_name T))) (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (declaration (decl_specifiers (atomic_specifier (type_name (decl_specifiers (typedef_name T))))) (init_declarator (declarator_name x))))))"));
        c_ast_test_expect(arguments, S8("typedef int T; void f(void) { _Alignas(T) char c; }"), S8("(translation_unit (declaration (decl_specifiers (specifier_word typedef) (specifier_word int)) (init_declarator (declarator_name T))) (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (declaration (decl_specifiers (alignas (type_name (decl_specifiers (typedef_name T)))) (specifier_word char)) (init_declarator (declarator_name c))))))"));
        c_ast_test_expect(arguments, S8("typedef int T; void f(void) { typeof(x) y; }"), S8("(translation_unit (declaration (decl_specifiers (specifier_word typedef) (specifier_word int)) (init_declarator (declarator_name T))) (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (declaration (decl_specifiers (typeof (identifier x))) (init_declarator (declarator_name y))))))"));
        c_ast_test_expect(arguments, S8("typedef int T; void f(void) { (x)(y); }"), S8("(translation_unit (declaration (decl_specifiers (specifier_word typedef) (specifier_word int)) (init_declarator (declarator_name T))) (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (call (identifier x) (identifier y))))))"));
        c_ast_test_expect(arguments, S8("typedef int T; void f(void) { (x)+y; }"), S8("(translation_unit (declaration (decl_specifiers (specifier_word typedef) (specifier_word int)) (init_declarator (declarator_name T))) (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (add (identifier x) (identifier y))))))"));
        c_ast_test_expect(arguments, S8("typedef int T; void f(void) { (x)-y; }"), S8("(translation_unit (declaration (decl_specifiers (specifier_word typedef) (specifier_word int)) (init_declarator (declarator_name T))) (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (subtract (identifier x) (identifier y))))))"));
        c_ast_test_expect(arguments, S8("typedef int T; void f(void) { (x)*y; }"), S8("(translation_unit (declaration (decl_specifiers (specifier_word typedef) (specifier_word int)) (init_declarator (declarator_name T))) (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (multiply (identifier x) (identifier y))))))"));
        c_ast_test_expect(arguments, S8("typedef int T; void f(void) { (x)&y; }"), S8("(translation_unit (declaration (decl_specifiers (specifier_word typedef) (specifier_word int)) (init_declarator (declarator_name T))) (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (bit_and (identifier x) (identifier y))))))"));
        c_ast_test_expect(arguments, S8("typedef int T; void f(void) { (x)[0]; }"), S8("(translation_unit (declaration (decl_specifiers (specifier_word typedef) (specifier_word int)) (init_declarator (declarator_name T))) (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (index (identifier x) (number 0))))))"));
        c_ast_test_expect(arguments, S8("typedef int T; void f(void) { (x).m; }"), S8("(translation_unit (declaration (decl_specifiers (specifier_word typedef) (specifier_word int)) (init_declarator (declarator_name T))) (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (member m (identifier x))))))"));

        // attributes: GNU
        c_ast_test_expect(arguments, S8("__attribute__((unused)) int x;"), S8("(translation_unit (declaration (decl_specifiers (attribute_list (attribute_gnu (attribute unused))) (specifier_word int)) (init_declarator (declarator_name x))))"));
        c_ast_test_expect(arguments, S8("__attribute__((aligned(16))) int x;"), S8("(translation_unit (declaration (decl_specifiers (attribute_list (attribute_gnu (attribute aligned (number 16)))) (specifier_word int)) (init_declarator (declarator_name x))))"));
        c_ast_test_expect(arguments, S8("__attribute__((unused, aligned(8))) int x;"), S8("(translation_unit (declaration (decl_specifiers (attribute_list (attribute_gnu (attribute unused) (attribute aligned (number 8)))) (specifier_word int)) (init_declarator (declarator_name x))))"));
        c_ast_test_expect(arguments, S8("__attribute__((a)) __attribute__((b)) int x;"), S8("(translation_unit (declaration (decl_specifiers (attribute_list (attribute_gnu (attribute a)) (attribute_gnu (attribute b))) (specifier_word int)) (init_declarator (declarator_name x))))"));
        c_ast_test_expect(arguments, S8("__attribute__(()) int x;"), S8("(translation_unit (declaration (decl_specifiers (attribute_list (attribute_gnu)) (specifier_word int)) (init_declarator (declarator_name x))))"));
        c_ast_test_expect(arguments, S8("__attribute__((section(\".text\"))) int x;"), S8("(translation_unit (declaration (decl_specifiers (attribute_list (attribute_gnu (attribute section (string \".text\")))) (specifier_word int)) (init_declarator (declarator_name x))))"));
        c_ast_test_expect(arguments, S8("__attribute__((aligned(sizeof(int)))) int x;"), S8("(translation_unit (declaration (decl_specifiers (attribute_list (attribute_gnu (attribute aligned (sizeof_type (type_name (decl_specifiers (specifier_word int))))))) (specifier_word int)) (init_declarator (declarator_name x))))"));
        c_ast_test_expect(arguments, S8("__attribute__((const)) int x;"), S8("(translation_unit (declaration (decl_specifiers (attribute_list (attribute_gnu (attribute const))) (specifier_word int)) (init_declarator (declarator_name x))))"));
        c_ast_test_expect(arguments, S8("__attribute__((type_tag_for_datatype(a, int))) int x;"), S8("(translation_unit (declaration (decl_specifiers (attribute_list (attribute_gnu (attribute type_tag_for_datatype (identifier a) (type_name (decl_specifiers (specifier_word int)))))) (specifier_word int)) (init_declarator (declarator_name x))))"));
        c_ast_test_expect(arguments, S8("__attribute__((format(printf, 1, 2))) int x;"), S8("(translation_unit (declaration (decl_specifiers (attribute_list (attribute_gnu (attribute format (identifier printf) (number 1) (number 2)))) (specifier_word int)) (init_declarator (declarator_name x))))"));
        c_ast_test_expect(arguments, S8("__attribute__((unused)) static int x;"), S8("(translation_unit (declaration (decl_specifiers (attribute_list (attribute_gnu (attribute unused))) (specifier_word static) (specifier_word int)) (init_declarator (declarator_name x))))"));
        c_ast_test_expect(arguments, S8("int __attribute__((unused)) x;"), S8("(translation_unit (declaration (decl_specifiers (specifier_word int) (attribute_list (attribute_gnu (attribute unused)))) (init_declarator (declarator_name x))))"));
        c_ast_test_expect(arguments, S8("int x __attribute__((aligned(16)));"), S8("(translation_unit (declaration (decl_specifiers (specifier_word int)) (init_declarator (declarator_name x) (attribute_list (attribute_gnu (attribute aligned (number 16)))))))"));
        c_ast_test_expect(arguments, S8("int x __attribute__((unused)) = 3;"), S8("(translation_unit (declaration (decl_specifiers (specifier_word int)) (init_declarator (declarator_name x) (attribute_list (attribute_gnu (attribute unused))) (number 3))))"));
        c_ast_test_expect(arguments, S8("int x __attribute__((unused)), y;"), S8("(translation_unit (declaration (decl_specifiers (specifier_word int)) (init_declarator (declarator_name x) (attribute_list (attribute_gnu (attribute unused)))) (init_declarator (declarator_name y))))"));
        c_ast_test_expect(arguments, S8("void f(void) __attribute__((noreturn));"), S8("(translation_unit (declaration (decl_specifiers (specifier_word void)) (init_declarator (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (attribute_list (attribute_gnu (attribute noreturn))))))"));
        c_ast_test_expect(arguments, S8("int f(void) __attribute__((nothrow)), g(void);"), S8("(translation_unit (declaration (decl_specifiers (specifier_word int)) (init_declarator (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (attribute_list (attribute_gnu (attribute nothrow)))) (init_declarator (declarator_function (declarator_name g) (parameter_list (parameter (decl_specifiers (specifier_word void))))))))"));
        c_ast_test_expect(arguments, S8("extern int f(const char *, ...) __attribute__((format(printf, 1, 2)));"), S8("(translation_unit (declaration (decl_specifiers (specifier_word extern) (specifier_word int)) (init_declarator (declarator_function (declarator_name f) (parameter_list_variadic (parameter (decl_specifiers (specifier_word const) (specifier_word char)) (declarator_pointer)))) (attribute_list (attribute_gnu (attribute format (identifier printf) (number 1) (number 2)))))))"));
        c_ast_test_expect(arguments, S8("__attribute__((format(printf, 1, 2))) void f(const char *, ...);"), S8("(translation_unit (declaration (decl_specifiers (attribute_list (attribute_gnu (attribute format (identifier printf) (number 1) (number 2)))) (specifier_word void)) (init_declarator (declarator_function (declarator_name f) (parameter_list_variadic (parameter (decl_specifiers (specifier_word const) (specifier_word char)) (declarator_pointer)))))))"));
        c_ast_test_expect(arguments, S8("__attribute__((visibility(\"hidden\"))) void f(void) { }"), S8("(translation_unit (function_definition (decl_specifiers (attribute_list (attribute_gnu (attribute visibility (string \"hidden\")))) (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement)))"));
        c_ast_test_expect(arguments, S8("__attribute__((aligned(16))) struct S { int a; } s;"), S8("(translation_unit (declaration (decl_specifiers (attribute_list (attribute_gnu (attribute aligned (number 16)))) (struct_specifier (tag_name S) (member_list (member_declaration (decl_specifiers (specifier_word int)) (member_declarator (declarator_name a)))))) (init_declarator (declarator_name s))))"));
        c_ast_test_expect(arguments, S8("typedef int T __attribute__((aligned(8)));"), S8("(translation_unit (declaration (decl_specifiers (specifier_word typedef) (specifier_word int)) (init_declarator (declarator_name T) (attribute_list (attribute_gnu (attribute aligned (number 8)))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { __attribute__((unused)) int x; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (declaration (decl_specifiers (attribute_list (attribute_gnu (attribute unused))) (specifier_word int)) (init_declarator (declarator_name x))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { int x __attribute__((cleanup(fn))) = 0; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (declaration (decl_specifiers (specifier_word int)) (init_declarator (declarator_name x) (attribute_list (attribute_gnu (attribute cleanup (identifier fn)))) (number 0))))))"));
        c_ast_test_expect(arguments, S8("__extension__ int x;"), S8("(translation_unit (declaration (decl_specifiers (specifier_word __extension__) (specifier_word int)) (init_declarator (declarator_name x))))"));
        c_ast_test_expect(arguments, S8("__extension__ typedef int T;"), S8("(translation_unit (declaration (decl_specifiers (specifier_word __extension__) (specifier_word typedef) (specifier_word int)) (init_declarator (declarator_name T))))"));

        // attributes: C23 [[ ]] and __declspec
        c_ast_test_expect(arguments, S8("[[deprecated(\"x\")]] int y;"), S8("(translation_unit (declaration (decl_specifiers (attribute_list (attribute_standard (attribute deprecated (string \"x\")))) (specifier_word int)) (init_declarator (declarator_name y))))"));
        c_ast_test_expect(arguments, S8("[[gnu::unused]] int z;"), S8("(translation_unit (declaration (decl_specifiers (attribute_list (attribute_standard (attribute_scoped unused (attribute_namespace gnu)))) (specifier_word int)) (init_declarator (declarator_name z))))"));
        c_ast_test_expect(arguments, S8("[[a, b]] int x;"), S8("(translation_unit (declaration (decl_specifiers (attribute_list (attribute_standard (attribute a) (attribute b))) (specifier_word int)) (init_declarator (declarator_name x))))"));
        c_ast_test_expect(arguments, S8("[[a, gnu::b(1)]] int x;"), S8("(translation_unit (declaration (decl_specifiers (attribute_list (attribute_standard (attribute a) (attribute_scoped b (attribute_namespace gnu) (number 1)))) (specifier_word int)) (init_declarator (declarator_name x))))"));
        // A vendor's standard attribute takes any balanced token sequence; the
        // item keeps no argument nodes. gnu and clang arguments stay
        // expressions.
        c_ast_test_expect(arguments, S8("[[vendor::tag(gnu::error(\"m\")), gnu::b(1)]] int x;"), S8("(translation_unit (declaration (decl_specifiers (attribute_list (attribute_standard (attribute_scoped tag (attribute_namespace vendor)) (attribute_scoped b (attribute_namespace gnu) (number 1)))) (specifier_word int)) (init_declarator (declarator_name x))))"));
        c_ast_test_expect(arguments, S8("[[vendor::tag(a[b[c]], {d; e}), x::y()]] int z;"), S8("(translation_unit (declaration (decl_specifiers (attribute_list (attribute_standard (attribute_scoped tag (attribute_namespace vendor)) (attribute_scoped y (attribute_namespace x)))) (specifier_word int)) (init_declarator (declarator_name z))))"));
        c_ast_test_expect_failure(arguments, S8("[[vendor::tag(a[b)]] int z;"));
        c_ast_test_expect(arguments, S8("[[a]] [[b]] int x;"), S8("(translation_unit (declaration (decl_specifiers (attribute_list (attribute_standard (attribute a)) (attribute_standard (attribute b))) (specifier_word int)) (init_declarator (declarator_name x))))"));
        c_ast_test_expect(arguments, S8("__attribute__((a)) [[b]] int x;"), S8("(translation_unit (declaration (decl_specifiers (attribute_list (attribute_gnu (attribute a)) (attribute_standard (attribute b))) (specifier_word int)) (init_declarator (declarator_name x))))"));
        c_ast_test_expect(arguments, S8("[[noreturn]] void f(void) { }"), S8("(translation_unit (function_definition (decl_specifiers (attribute_list (attribute_standard (attribute noreturn))) (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement)))"));
        c_ast_oracle_expect_dialect(arguments, C_PREPROCESS_DIALECT_C23, S8("[[]] int x;"), S8("(translation_unit (declaration (decl_specifiers (attribute_list (attribute_standard)) (specifier_word int)) (init_declarator (declarator_name x))))"));
        c_ast_oracle_expect_dialect(arguments, C_PREPROCESS_DIALECT_C23, S8("[[gnu::unused]] int x;"), S8("(translation_unit (declaration (decl_specifiers (attribute_list (attribute_standard (attribute_scoped unused (attribute_namespace gnu)))) (specifier_word int)) (init_declarator (declarator_name x))))"));
        c_ast_oracle_expect_dialect(arguments, C_PREPROCESS_DIALECT_C23, S8("struct [[gnu::packed]] S { int a; };"), S8("(translation_unit (declaration (decl_specifiers (struct_specifier (attribute_list (attribute_standard (attribute_scoped packed (attribute_namespace gnu)))) (tag_name S) (member_list (member_declaration (decl_specifiers (specifier_word int)) (member_declarator (declarator_name a))))))))"));
        c_ast_oracle_expect_dialect(arguments, C_PREPROCESS_DIALECT_C23, S8("void f(void) { [[fallthrough]]; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (attribute_statement (attribute_list (attribute_standard (attribute fallthrough)))))))"));
        c_ast_oracle_expect_dialect(arguments, C_PREPROCESS_DIALECT_C23, S8("void f(void) { [[likely]] if (a) x; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (attributed_statement (attribute_list (attribute_standard (attribute likely))) (if (identifier a) (expression_statement (identifier x)))))))"));
        c_ast_oracle_expect_dialect(arguments, C_PREPROCESS_DIALECT_C23, S8("void f(void) { [[gnu::cold]] x = 1; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (attributed_statement (attribute_list (attribute_standard (attribute_scoped cold (attribute_namespace gnu)))) (expression_statement (assign (identifier x) (number 1)))))))"));
        c_ast_oracle_expect_dialect(arguments, C_PREPROCESS_DIALECT_C23, S8("void f(void) { [[a]] [[b]] return; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (attributed_statement (attribute_list (attribute_standard (attribute a)) (attribute_standard (attribute b))) (return)))))"));
        c_ast_test_expect(arguments, S8("__declspec(dllexport) int x;"), S8("(translation_unit (declaration (decl_specifiers (attribute_list (attribute_declspec (attribute dllexport))) (specifier_word int)) (init_declarator (declarator_name x))))"));
        c_ast_test_expect(arguments, S8("__declspec(noinline dllexport) void f(void);"), S8("(translation_unit (declaration (decl_specifiers (attribute_list (attribute_declspec (attribute noinline) (attribute dllexport))) (specifier_word void)) (init_declarator (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))))))"));
        c_ast_test_expect(arguments, S8("__declspec(dllexport) __attribute__((unused)) int x;"), S8("(translation_unit (declaration (decl_specifiers (attribute_list (attribute_declspec (attribute dllexport)) (attribute_gnu (attribute unused))) (specifier_word int)) (init_declarator (declarator_name x))))"));

        // assembly
        c_ast_test_expect(arguments, S8("void f(void) { asm(\"nop\"); }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (asm (string \"nop\")))))"));
        c_ast_test_expect(arguments, S8("void f(void) { asm volatile (\"nop\"); }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (asm volatile (string \"nop\")))))"));
        c_ast_test_expect(arguments, S8("void f(void) { __asm__ __volatile__ (\"nop\"); }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (asm volatile (string \"nop\")))))"));
        c_ast_test_expect(arguments, S8("void f(void) { __asm__(\"nop\"); }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (asm (string \"nop\")))))"));
        c_ast_test_expect(arguments, S8("void f(void) { asm inline (\"nop\"); }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (asm inline (string \"nop\")))))"));
        c_ast_test_expect(arguments, S8("void f(void) { asm volatile inline (\"nop\"); }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (asm volatile inline (string \"nop\")))))"));
        c_ast_test_expect(arguments, S8("void f(void) { asm volatile (\"nop\" : \"=r\"(x) : [in] \"r\"(y) : \"memory\"); }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (asm volatile (string \"nop\") (asm_outputs (asm_operand (string \"=r\") (identifier x))) (asm_inputs (asm_operand (asm_symbolic_name in) (string \"r\") (identifier y))) (asm_clobbers (string \"memory\"))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { asm(\"mov %0, %1\" : \"=r\"(a) : \"r\"(b)); }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (asm (string \"mov %0, %1\") (asm_outputs (asm_operand (string \"=r\") (identifier a))) (asm_inputs (asm_operand (string \"r\") (identifier b)))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { asm(\"\" : \"=r\"(a), \"=m\"(b[0])); }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (asm (string \"\") (asm_outputs (asm_operand (string \"=r\") (identifier a)) (asm_operand (string \"=m\") (index (identifier b) (number 0))))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { asm(\"\" : : \"r\"(x + 1)); }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (asm (string \"\") (asm_outputs) (asm_inputs (asm_operand (string \"r\") (add (identifier x) (number 1))))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { asm(\"\" : : [a] \"r\"(x), [b] \"i\"(1)); }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (asm (string \"\") (asm_outputs) (asm_inputs (asm_operand (asm_symbolic_name a) (string \"r\") (identifier x)) (asm_operand (asm_symbolic_name b) (string \"i\") (number 1)))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { asm(\"\" : : : \"memory\", \"cc\"); }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (asm (string \"\") (asm_outputs) (asm_inputs) (asm_clobbers (string \"memory\") (string \"cc\"))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { asm volatile (\"\" ::: \"memory\"); }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (asm volatile (string \"\") (asm_outputs) (asm_inputs) (asm_clobbers (string \"memory\"))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { asm(\"a\" \"b\"); }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (asm (string \"a\" \"b\")))))"));
        c_ast_test_expect(arguments, S8("void f(void) { asm(\"\" : : :); }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (asm (string \"\") (asm_outputs) (asm_inputs) (asm_clobbers)))))"));
        c_ast_test_expect(arguments, S8("void f(void) { asm goto (\"jmp %l0\" : : : : done); }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (asm goto (string \"jmp %l0\") (asm_outputs) (asm_inputs) (asm_clobbers) (asm_labels (identifier done))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { asm goto (\"\" : : : : l1, l2); }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (asm goto (string \"\") (asm_outputs) (asm_inputs) (asm_clobbers) (asm_labels (identifier l1) (identifier l2))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { asm volatile goto (\"\" : : \"r\"(x) : \"memory\" : l); }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (asm volatile goto (string \"\") (asm_outputs) (asm_inputs (asm_operand (string \"r\") (identifier x))) (asm_clobbers (string \"memory\")) (asm_labels (identifier l))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { l: asm(\"nop\"); }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (labeled l (asm (string \"nop\"))))))"));
        c_ast_test_expect(arguments, S8("asm(\"nop\");"), S8("(translation_unit (asm_top_level (string \"nop\")))"));
        c_ast_test_expect(arguments, S8("__asm__(\"nop\");"), S8("(translation_unit (asm_top_level (string \"nop\")))"));
        c_ast_test_expect(arguments, S8("asm(\"a\" \"b\");"), S8("(translation_unit (asm_top_level (string \"a\" \"b\")))"));
        c_ast_test_expect(arguments, S8("int x asm(\"y\");"), S8("(translation_unit (declaration (decl_specifiers (specifier_word int)) (init_declarator (declarator_name x) (asm_label (string \"y\")))))"));
        c_ast_test_expect(arguments, S8("int f(void) asm(\"g\");"), S8("(translation_unit (declaration (decl_specifiers (specifier_word int)) (init_declarator (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (asm_label (string \"g\")))))"));
        c_ast_test_expect(arguments, S8("int x asm(\"y\") __attribute__((unused)) = 1;"), S8("(translation_unit (declaration (decl_specifiers (specifier_word int)) (init_declarator (declarator_name x) (asm_label (string \"y\")) (attribute_list (attribute_gnu (attribute unused))) (number 1))))"));
        c_ast_test_expect(arguments, S8("int x asm(\"y\"), z asm(\"w\");"), S8("(translation_unit (declaration (decl_specifiers (specifier_word int)) (init_declarator (declarator_name x) (asm_label (string \"y\"))) (init_declarator (declarator_name z) (asm_label (string \"w\")))))"));

        // C23 keywords and forms (dialect C23)
        c_ast_oracle_expect_dialect(arguments, C_PREPROCESS_DIALECT_C23, S8("void f(void) { x = true; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (assign (identifier x) (boolean_constant true))))))"));
        c_ast_oracle_expect_dialect(arguments, C_PREPROCESS_DIALECT_C23, S8("void f(void) { x = false; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (assign (identifier x) (boolean_constant false))))))"));
        c_ast_oracle_expect_dialect(arguments, C_PREPROCESS_DIALECT_C23, S8("void f(void) { p = nullptr; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (assign (identifier p) (nullptr))))))"));
        c_ast_oracle_expect_dialect(arguments, C_PREPROCESS_DIALECT_C23, S8("void f(void) { constexpr int x = 1; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (declaration (decl_specifiers (specifier_word constexpr) (specifier_word int)) (init_declarator (declarator_name x) (number 1))))))"));
        c_ast_oracle_expect_dialect(arguments, C_PREPROCESS_DIALECT_C23, S8("void f(void) { auto x = 1; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (declaration (decl_specifiers (specifier_word auto)) (init_declarator (declarator_name x) (number 1))))))"));
        c_ast_oracle_expect_dialect(arguments, C_PREPROCESS_DIALECT_C23, S8("void f(void) { alignas(16) int x; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (declaration (decl_specifiers (alignas (number 16)) (specifier_word int)) (init_declarator (declarator_name x))))))"));
        c_ast_oracle_expect_dialect(arguments, C_PREPROCESS_DIALECT_C23, S8("void f(void) { alignas(int) char c; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (declaration (decl_specifiers (alignas (type_name (decl_specifiers (specifier_word int)))) (specifier_word char)) (init_declarator (declarator_name c))))))"));
        c_ast_oracle_expect_dialect(arguments, C_PREPROCESS_DIALECT_C23, S8("void f(void) { x = alignof(int); }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (assign (identifier x) (alignof_type (type_name (decl_specifiers (specifier_word int)))))))))"));
        c_ast_oracle_expect_dialect(arguments, C_PREPROCESS_DIALECT_C23, S8("void f(void) { thread_local int x; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (declaration (decl_specifiers (specifier_word _Thread_local) (specifier_word int)) (init_declarator (declarator_name x))))))"));
        c_ast_oracle_expect_dialect(arguments, C_PREPROCESS_DIALECT_C23, S8("void f(void) { typeof_unqual(x) y; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (declaration (decl_specifiers (typeof_unqual (identifier x))) (init_declarator (declarator_name y))))))"));
        c_ast_oracle_expect_dialect(arguments, C_PREPROCESS_DIALECT_C23, S8("void f(void) { typeof(x) y; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (declaration (decl_specifiers (typeof (identifier x))) (init_declarator (declarator_name y))))))"));
        c_ast_oracle_expect_dialect(arguments, C_PREPROCESS_DIALECT_C23, S8("void f(void) { typeof(int *) p; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (declaration (decl_specifiers (typeof (type_name (decl_specifiers (specifier_word int)) (declarator_pointer)))) (init_declarator (declarator_name p))))))"));
        c_ast_oracle_expect_dialect(arguments, C_PREPROCESS_DIALECT_C23, S8("void f(void) { typeof_unqual(1, x) y; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (declaration (decl_specifiers (typeof_unqual (comma (number 1) (identifier x)))) (init_declarator (declarator_name y))))))"));
        c_ast_oracle_expect_dialect(arguments, C_PREPROCESS_DIALECT_C23, S8("void f(void) { _BitInt(8) x; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (declaration (decl_specifiers (bitint (number 8))) (init_declarator (declarator_name x))))))"));
        c_ast_oracle_expect_dialect(arguments, C_PREPROCESS_DIALECT_C23, S8("void f(void) { unsigned _BitInt(32) y; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (declaration (decl_specifiers (specifier_word unsigned) (bitint (number 32))) (init_declarator (declarator_name y))))))"));
        c_ast_oracle_expect_dialect(arguments, C_PREPROCESS_DIALECT_C23, S8("void f(void) { switch (a) { case 1: } }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (switch (identifier a) (compound_statement (case (number 1)))))))"));
        c_ast_oracle_expect_dialect(arguments, C_PREPROCESS_DIALECT_C23, S8("void f(void) { switch (a) { case 1: x; break; case 2: } }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (switch (identifier a) (compound_statement (case (number 1) (expression_statement (identifier x))) (break) (case (number 2)))))))"));
        c_ast_oracle_expect_dialect(arguments, C_PREPROCESS_DIALECT_C23, S8("void f(void) { l: }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (labeled l))))"));
        c_ast_oracle_expect_dialect(arguments, C_PREPROCESS_DIALECT_C23, S8("void f(...);"), S8("(translation_unit (declaration (decl_specifiers (specifier_word void)) (init_declarator (declarator_function (declarator_name f) (parameter_list_variadic)))))"));
        c_ast_oracle_expect_dialect(arguments, C_PREPROCESS_DIALECT_C23, S8("enum E : unsigned { A };"), S8("(translation_unit (declaration (decl_specifiers (enum_specifier (tag_name E) (type_name (decl_specifiers (specifier_word unsigned))) (enumerator_list (enumerator A))))))"));
        c_ast_oracle_expect_dialect(arguments, C_PREPROCESS_DIALECT_C23, S8("enum E : unsigned char { A, B };"), S8("(translation_unit (declaration (decl_specifiers (enum_specifier (tag_name E) (type_name (decl_specifiers (specifier_word unsigned) (specifier_word char))) (enumerator_list (enumerator A) (enumerator B))))))"));
        c_ast_oracle_expect_dialect(arguments, C_PREPROCESS_DIALECT_C23, S8("enum E : int;"), S8("(translation_unit (declaration (decl_specifiers (enum_specifier (tag_name E) (type_name (decl_specifiers (specifier_word int)))))))"));
        c_ast_oracle_expect_dialect(arguments, C_PREPROCESS_DIALECT_C23, S8("enum : int { A };"), S8("(translation_unit (declaration (decl_specifiers (enum_specifier (type_name (decl_specifiers (specifier_word int))) (enumerator_list (enumerator A))))))"));
        c_ast_oracle_expect_dialect(arguments, C_PREPROCESS_DIALECT_C23, S8("struct S { int a; static_assert(1, \"m\"); };"), S8("(translation_unit (declaration (decl_specifiers (struct_specifier (tag_name S) (member_list (member_declaration (decl_specifiers (specifier_word int)) (member_declarator (declarator_name a))) (static_assert (number 1) (string \"m\")))))))"));
        c_ast_oracle_expect_dialect(arguments, C_PREPROCESS_DIALECT_C23, S8("void f(void) { int a[] = {}; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (declaration (decl_specifiers (specifier_word int)) (init_declarator (declarator_array (declarator_name a)) (initializer_list))))))"));
        c_ast_oracle_expect_dialect(arguments, C_PREPROCESS_DIALECT_GNU23, S8("void f(void) { asm volatile (\"nop\"); }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (asm volatile (string \"nop\")))))"));
        c_ast_oracle_expect_dialect(arguments, C_PREPROCESS_DIALECT_GNU23, S8("void f(void) { x = true; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (assign (identifier x) (boolean_constant true))))))"));
        c_ast_oracle_expect_dialect(arguments, C_PREPROCESS_DIALECT_GNU23, S8("void f(void) { typeof(x) y; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (declaration (decl_specifiers (typeof (identifier x))) (init_declarator (declarator_name y))))))"));

        // dialect gating: C23-only words are ordinary identifiers in the default (GNU17) dialect
        c_ast_test_expect(arguments, S8("void f(void) { x = true; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (assign (identifier x) (identifier true))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { x = false; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (assign (identifier x) (identifier false))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { p = nullptr; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (assign (identifier p) (identifier nullptr))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { f(true, false); }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (call (identifier f) (identifier true) (identifier false))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { x = alignof(y); }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (assign (identifier x) (call (identifier alignof) (identifier y)))))))"));
        c_ast_oracle_expect_dialect(arguments, C_PREPROCESS_DIALECT_C11, S8("void f(void) { x = true; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (assign (identifier x) (identifier true))))))"));
        c_ast_oracle_expect_dialect(arguments, C_PREPROCESS_DIALECT_C17, S8("void f(void) { x = true; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (assign (identifier x) (identifier true))))))"));

        // deep nesting (explicit stacks, no recursion)
        c_ast_test_expect(arguments, S8("void f(void) { x = ((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((a)))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))); }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (assign (identifier x) (identifier a))))))"));
        c_ast_test_expect(arguments, S8("void f(void) {{{{{{{{{{{{{{{{{{{{{{{{{{{{{{{{{{{{{{{{{{{{{{{{{{{{{{{{{{{{{{{{{{{{{{{{{{{{{{{{{{{{{{{{{{{{{{{{{{{{x;}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (compound_statement (compound_statement (compound_statement (compound_statement (compound_statement (compound_statement (compound_statement (compound_statement (compound_statement (compound_statement (compound_statement (compound_statement (compound_statement (compound_statement (compound_statement (compound_statement (compound_statement (compound_statement (compound_statement (compound_statement (compound_statement (compound_statement (compound_statement (compound_statement (compound_statement (compound_statement (compound_statement (compound_statement (compound_statement (compound_statement (compound_statement (compound_statement (compound_statement (compound_statement (compound_statement (compound_statement (compound_statement (compound_statement (compound_statement (compound_statement (compound_statement (compound_statement (compound_statement (compound_statement (compound_statement (compound_statement (compound_statement (compound_statement (compound_statement (compound_statement (compound_statement (compound_statement (compound_statement (compound_statement (compound_statement (compound_statement (compound_statement (compound_statement (compound_statement (compound_statement (compound_statement (compound_statement (compound_statement (compound_statement (compound_statement (compound_statement (compound_statement (compound_statement (compound_statement (compound_statement (compound_statement (compound_statement (compound_statement (compound_statement (compound_statement (compound_statement (compound_statement (compound_statement (compound_statement (compound_statement (compound_statement (compound_statement (compound_statement (compound_statement (compound_statement (compound_statement (compound_statement (compound_statement (compound_statement (compound_statement (compound_statement (compound_statement (compound_statement (compound_statement (compound_statement (compound_statement (compound_statement (compound_statement (compound_statement (expression_statement (identifier x))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))"));
        c_ast_test_expect(arguments, S8("int ****************************************************************************************************p;"), S8("(translation_unit (declaration (decl_specifiers (specifier_word int)) (init_declarator (declarator_pointer (declarator_pointer (declarator_pointer (declarator_pointer (declarator_pointer (declarator_pointer (declarator_pointer (declarator_pointer (declarator_pointer (declarator_pointer (declarator_pointer (declarator_pointer (declarator_pointer (declarator_pointer (declarator_pointer (declarator_pointer (declarator_pointer (declarator_pointer (declarator_pointer (declarator_pointer (declarator_pointer (declarator_pointer (declarator_pointer (declarator_pointer (declarator_pointer (declarator_pointer (declarator_pointer (declarator_pointer (declarator_pointer (declarator_pointer (declarator_pointer (declarator_pointer (declarator_pointer (declarator_pointer (declarator_pointer (declarator_pointer (declarator_pointer (declarator_pointer (declarator_pointer (declarator_pointer (declarator_pointer (declarator_pointer (declarator_pointer (declarator_pointer (declarator_pointer (declarator_pointer (declarator_pointer (declarator_pointer (declarator_pointer (declarator_pointer (declarator_pointer (declarator_pointer (declarator_pointer (declarator_pointer (declarator_pointer (declarator_pointer (declarator_pointer (declarator_pointer (declarator_pointer (declarator_pointer (declarator_pointer (declarator_pointer (declarator_pointer (declarator_pointer (declarator_pointer (declarator_pointer (declarator_pointer (declarator_pointer (declarator_pointer (declarator_pointer (declarator_pointer (declarator_pointer (declarator_pointer (declarator_pointer (declarator_pointer (declarator_pointer (declarator_pointer (declarator_pointer (declarator_pointer (declarator_pointer (declarator_pointer (declarator_pointer (declarator_pointer (declarator_pointer (declarator_pointer (declarator_pointer (declarator_pointer (declarator_pointer (declarator_pointer (declarator_pointer (declarator_pointer (declarator_pointer (declarator_pointer (declarator_pointer (declarator_pointer (declarator_pointer (declarator_pointer (declarator_pointer (declarator_pointer (declarator_pointer (declarator_name p))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { x = - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - a; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (assign (identifier x) (negate (negate (negate (negate (negate (negate (negate (negate (negate (negate (negate (negate (negate (negate (negate (negate (negate (negate (negate (negate (negate (negate (negate (negate (negate (negate (negate (negate (negate (negate (negate (negate (negate (negate (negate (negate (negate (negate (negate (negate (negate (negate (negate (negate (negate (negate (negate (negate (negate (negate (negate (negate (negate (negate (negate (negate (negate (negate (negate (negate (negate (negate (negate (negate (negate (negate (negate (negate (negate (negate (negate (negate (negate (negate (negate (negate (negate (negate (negate (negate (negate (negate (negate (negate (negate (negate (negate (negate (negate (negate (negate (negate (negate (negate (negate (negate (negate (negate (negate (negate (identifier a))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a + a; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (add (add (add (add (add (add (add (add (add (add (add (add (add (add (add (add (add (add (add (add (add (add (add (add (add (add (add (add (add (add (add (add (add (add (add (add (add (add (add (add (add (add (add (add (add (add (add (add (add (add (add (add (add (add (add (add (add (add (add (add (add (add (add (add (add (add (add (add (add (add (add (add (add (add (add (add (add (add (add (add (add (add (add (add (add (add (add (add (add (add (add (add (add (add (add (add (add (add (add (identifier a) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { a, a, a, a, a, a, a, a, a, a, a, a, a, a, a, a, a, a, a, a, a, a, a, a, a, a, a, a, a, a, a, a, a, a, a, a, a, a, a, a, a, a, a, a, a, a, a, a, a, a, a, a, a, a, a, a, a, a, a, a, a, a, a, a, a, a, a, a, a, a, a, a, a, a, a, a, a, a, a, a, a, a, a, a, a, a, a, a, a, a, a, a, a, a, a, a, a, a, a, a; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (comma (comma (comma (comma (comma (comma (comma (comma (comma (comma (comma (comma (comma (comma (comma (comma (comma (comma (comma (comma (comma (comma (comma (comma (comma (comma (comma (comma (comma (comma (comma (comma (comma (comma (comma (comma (comma (comma (comma (comma (comma (comma (comma (comma (comma (comma (comma (comma (comma (comma (comma (comma (comma (comma (comma (comma (comma (comma (comma (comma (comma (comma (comma (comma (comma (comma (comma (comma (comma (comma (comma (comma (comma (comma (comma (comma (comma (comma (comma (comma (comma (comma (comma (comma (comma (comma (comma (comma (comma (comma (comma (comma (comma (comma (comma (comma (comma (comma (comma (identifier a) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { a = a = a = a = a = a = a = a = a = a = a = a = a = a = a = a = a = a = a = a = a = a = a = a = a = a = a = a = a = a = a = a = a = a = a = a = a = a = a = a = a = a = a = a = a = a = a = a = a = a = a = a = a = a = a = a = a = a = a = a = a = a = a = a = a = a = a = a = a = a = a = a = a = a = a = a = a = a = a = a = a = a = a = a = a = a = a = a = a = a = a = a = a = a = a = a = a = a = a = a; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (assign (identifier a) (assign (identifier a) (assign (identifier a) (assign (identifier a) (assign (identifier a) (assign (identifier a) (assign (identifier a) (assign (identifier a) (assign (identifier a) (assign (identifier a) (assign (identifier a) (assign (identifier a) (assign (identifier a) (assign (identifier a) (assign (identifier a) (assign (identifier a) (assign (identifier a) (assign (identifier a) (assign (identifier a) (assign (identifier a) (assign (identifier a) (assign (identifier a) (assign (identifier a) (assign (identifier a) (assign (identifier a) (assign (identifier a) (assign (identifier a) (assign (identifier a) (assign (identifier a) (assign (identifier a) (assign (identifier a) (assign (identifier a) (assign (identifier a) (assign (identifier a) (assign (identifier a) (assign (identifier a) (assign (identifier a) (assign (identifier a) (assign (identifier a) (assign (identifier a) (assign (identifier a) (assign (identifier a) (assign (identifier a) (assign (identifier a) (assign (identifier a) (assign (identifier a) (assign (identifier a) (assign (identifier a) (assign (identifier a) (assign (identifier a) (assign (identifier a) (assign (identifier a) (assign (identifier a) (assign (identifier a) (assign (identifier a) (assign (identifier a) (assign (identifier a) (assign (identifier a) (assign (identifier a) (assign (identifier a) (assign (identifier a) (assign (identifier a) (assign (identifier a) (assign (identifier a) (assign (identifier a) (assign (identifier a) (assign (identifier a) (assign (identifier a) (assign (identifier a) (assign (identifier a) (assign (identifier a) (assign (identifier a) (assign (identifier a) (assign (identifier a) (assign (identifier a) (assign (identifier a) (assign (identifier a) (assign (identifier a) (assign (identifier a) (assign (identifier a) (assign (identifier a) (assign (identifier a) (assign (identifier a) (assign (identifier a) (assign (identifier a) (assign (identifier a) (assign (identifier a) (assign (identifier a) (assign (identifier a) (assign (identifier a) (assign (identifier a) (assign (identifier a) (assign (identifier a) (assign (identifier a) (assign (identifier a) (assign (identifier a) (assign (identifier a) (assign (identifier a) (assign (identifier a) (identifier a))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { a ? a : a ? a : a ? a : a ? a : a ? a : a ? a : a ? a : a ? a : a ? a : a ? a : a ? a : a ? a : a ? a : a ? a : a ? a : a ? a : a ? a : a ? a : a ? a : a ? a : a ? a : a ? a : a ? a : a ? a : a ? a : a ? a : a ? a : a ? a : a ? a : a ? a : a ? a : a ? a : a ? a : a ? a : a ? a : a ? a : a ? a : a ? a : a ? a : a ? a : a ? a : a ? a : a ? a : a ? a : a ? a : a ? a : a ? a : a ? a : a ? a : a ? a : a ? a : a ? a : a ? a : a ? a : a ? a : a ? a : a ? a : a ? a : a ? a : a ? a : a; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (conditional (identifier a) (identifier a) (conditional (identifier a) (identifier a) (conditional (identifier a) (identifier a) (conditional (identifier a) (identifier a) (conditional (identifier a) (identifier a) (conditional (identifier a) (identifier a) (conditional (identifier a) (identifier a) (conditional (identifier a) (identifier a) (conditional (identifier a) (identifier a) (conditional (identifier a) (identifier a) (conditional (identifier a) (identifier a) (conditional (identifier a) (identifier a) (conditional (identifier a) (identifier a) (conditional (identifier a) (identifier a) (conditional (identifier a) (identifier a) (conditional (identifier a) (identifier a) (conditional (identifier a) (identifier a) (conditional (identifier a) (identifier a) (conditional (identifier a) (identifier a) (conditional (identifier a) (identifier a) (conditional (identifier a) (identifier a) (conditional (identifier a) (identifier a) (conditional (identifier a) (identifier a) (conditional (identifier a) (identifier a) (conditional (identifier a) (identifier a) (conditional (identifier a) (identifier a) (conditional (identifier a) (identifier a) (conditional (identifier a) (identifier a) (conditional (identifier a) (identifier a) (conditional (identifier a) (identifier a) (conditional (identifier a) (identifier a) (conditional (identifier a) (identifier a) (conditional (identifier a) (identifier a) (conditional (identifier a) (identifier a) (conditional (identifier a) (identifier a) (conditional (identifier a) (identifier a) (conditional (identifier a) (identifier a) (conditional (identifier a) (identifier a) (conditional (identifier a) (identifier a) (conditional (identifier a) (identifier a) (conditional (identifier a) (identifier a) (conditional (identifier a) (identifier a) (conditional (identifier a) (identifier a) (conditional (identifier a) (identifier a) (conditional (identifier a) (identifier a) (conditional (identifier a) (identifier a) (conditional (identifier a) (identifier a) (conditional (identifier a) (identifier a) (conditional (identifier a) (identifier a) (conditional (identifier a) (identifier a) (conditional (identifier a) (identifier a) (conditional (identifier a) (identifier a) (conditional (identifier a) (identifier a) (conditional (identifier a) (identifier a) (conditional (identifier a) (identifier a) (conditional (identifier a) (identifier a) (conditional (identifier a) (identifier a) (conditional (identifier a) (identifier a) (conditional (identifier a) (identifier a) (conditional (identifier a) (identifier a) (identifier a)))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { a ? a ? a ? a ? a ? a ? a ? a ? a ? a ? a ? a ? a ? a ? a ? a ? a ? a ? a ? a ? a ? a ? a ? a ? a ? a ? a ? a ? a ? a ? a ? a ? a ? a ? a ? a ? a ? a ? a ? a ? a ? a ? a ? a ? a ? a ? a ? a ? a ? a ? a ? a ? a ? a ? a ? a ? a ? a ? a ? a ? a : a : a : a : a : a : a : a : a : a : a : a : a : a : a : a : a : a : a : a : a : a : a : a : a : a : a : a : a : a : a : a : a : a : a : a : a : a : a : a : a : a : a : a : a : a : a : a : a : a : a : a : a : a : a : a : a : a : a : a : a; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (conditional (identifier a) (conditional (identifier a) (conditional (identifier a) (conditional (identifier a) (conditional (identifier a) (conditional (identifier a) (conditional (identifier a) (conditional (identifier a) (conditional (identifier a) (conditional (identifier a) (conditional (identifier a) (conditional (identifier a) (conditional (identifier a) (conditional (identifier a) (conditional (identifier a) (conditional (identifier a) (conditional (identifier a) (conditional (identifier a) (conditional (identifier a) (conditional (identifier a) (conditional (identifier a) (conditional (identifier a) (conditional (identifier a) (conditional (identifier a) (conditional (identifier a) (conditional (identifier a) (conditional (identifier a) (conditional (identifier a) (conditional (identifier a) (conditional (identifier a) (conditional (identifier a) (conditional (identifier a) (conditional (identifier a) (conditional (identifier a) (conditional (identifier a) (conditional (identifier a) (conditional (identifier a) (conditional (identifier a) (conditional (identifier a) (conditional (identifier a) (conditional (identifier a) (conditional (identifier a) (conditional (identifier a) (conditional (identifier a) (conditional (identifier a) (conditional (identifier a) (conditional (identifier a) (conditional (identifier a) (conditional (identifier a) (conditional (identifier a) (conditional (identifier a) (conditional (identifier a) (conditional (identifier a) (conditional (identifier a) (conditional (identifier a) (conditional (identifier a) (conditional (identifier a) (conditional (identifier a) (conditional (identifier a) (conditional (identifier a) (identifier a) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a)) (identifier a))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { f(f(f(f(f(f(f(f(f(f(f(f(f(f(f(f(f(f(f(f(f(f(f(f(f(f(f(f(f(f(f(f(f(f(f(f(f(f(f(f(f(f(f(f(f(f(f(f(f(f(f(f(f(f(f(f(f(f(f(f(f(f(f(f(f(f(f(f(f(f(f(f(f(f(f(f(f(f(f(f(a)))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))); }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (call (identifier f) (call (identifier f) (call (identifier f) (call (identifier f) (call (identifier f) (call (identifier f) (call (identifier f) (call (identifier f) (call (identifier f) (call (identifier f) (call (identifier f) (call (identifier f) (call (identifier f) (call (identifier f) (call (identifier f) (call (identifier f) (call (identifier f) (call (identifier f) (call (identifier f) (call (identifier f) (call (identifier f) (call (identifier f) (call (identifier f) (call (identifier f) (call (identifier f) (call (identifier f) (call (identifier f) (call (identifier f) (call (identifier f) (call (identifier f) (call (identifier f) (call (identifier f) (call (identifier f) (call (identifier f) (call (identifier f) (call (identifier f) (call (identifier f) (call (identifier f) (call (identifier f) (call (identifier f) (call (identifier f) (call (identifier f) (call (identifier f) (call (identifier f) (call (identifier f) (call (identifier f) (call (identifier f) (call (identifier f) (call (identifier f) (call (identifier f) (call (identifier f) (call (identifier f) (call (identifier f) (call (identifier f) (call (identifier f) (call (identifier f) (call (identifier f) (call (identifier f) (call (identifier f) (call (identifier f) (call (identifier f) (call (identifier f) (call (identifier f) (call (identifier f) (call (identifier f) (call (identifier f) (call (identifier f) (call (identifier f) (call (identifier f) (call (identifier f) (call (identifier f) (call (identifier f) (call (identifier f) (call (identifier f) (call (identifier f) (call (identifier f) (call (identifier f) (call (identifier f) (call (identifier f) (call (identifier f) (identifier a)))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { a[1][1][1][1][1][1][1][1][1][1][1][1][1][1][1][1][1][1][1][1][1][1][1][1][1][1][1][1][1][1][1][1][1][1][1][1][1][1][1][1][1][1][1][1][1][1][1][1][1][1][1][1][1][1][1][1][1][1][1][1][1][1][1][1][1][1][1][1][1][1][1][1][1][1][1][1][1][1][1][1]; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (index (index (index (index (index (index (index (index (index (index (index (index (index (index (index (index (index (index (index (index (index (index (index (index (index (index (index (index (index (index (index (index (index (index (index (index (index (index (index (index (index (index (index (index (index (index (index (index (index (index (index (index (index (index (index (index (index (index (index (index (index (index (index (index (index (index (index (index (index (index (index (index (index (index (index (index (index (index (index (index (identifier a) (number 1)) (number 1)) (number 1)) (number 1)) (number 1)) (number 1)) (number 1)) (number 1)) (number 1)) (number 1)) (number 1)) (number 1)) (number 1)) (number 1)) (number 1)) (number 1)) (number 1)) (number 1)) (number 1)) (number 1)) (number 1)) (number 1)) (number 1)) (number 1)) (number 1)) (number 1)) (number 1)) (number 1)) (number 1)) (number 1)) (number 1)) (number 1)) (number 1)) (number 1)) (number 1)) (number 1)) (number 1)) (number 1)) (number 1)) (number 1)) (number 1)) (number 1)) (number 1)) (number 1)) (number 1)) (number 1)) (number 1)) (number 1)) (number 1)) (number 1)) (number 1)) (number 1)) (number 1)) (number 1)) (number 1)) (number 1)) (number 1)) (number 1)) (number 1)) (number 1)) (number 1)) (number 1)) (number 1)) (number 1)) (number 1)) (number 1)) (number 1)) (number 1)) (number 1)) (number 1)) (number 1)) (number 1)) (number 1)) (number 1)) (number 1)) (number 1)) (number 1)) (number 1)) (number 1)) (number 1))))))"));
        c_ast_test_expect(arguments, S8("int a = {{{{{{{{{{{{{{{{{{{{{{{{{{{{{{{{{{{{{{{{{{{{{{{{{{1}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}};"), S8("(translation_unit (declaration (decl_specifiers (specifier_word int)) (init_declarator (declarator_name a) (initializer_list (initializer_list (initializer_list (initializer_list (initializer_list (initializer_list (initializer_list (initializer_list (initializer_list (initializer_list (initializer_list (initializer_list (initializer_list (initializer_list (initializer_list (initializer_list (initializer_list (initializer_list (initializer_list (initializer_list (initializer_list (initializer_list (initializer_list (initializer_list (initializer_list (initializer_list (initializer_list (initializer_list (initializer_list (initializer_list (initializer_list (initializer_list (initializer_list (initializer_list (initializer_list (initializer_list (initializer_list (initializer_list (initializer_list (initializer_list (initializer_list (initializer_list (initializer_list (initializer_list (initializer_list (initializer_list (initializer_list (initializer_list (initializer_list (initializer_list (number 1))))))))))))))))))))))))))))))))))))))))))))))))))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { if (a) x; else if (a) x; else if (a) x; else if (a) x; else if (a) x; else if (a) x; else if (a) x; else if (a) x; else if (a) x; else if (a) x; else if (a) x; else if (a) x; else if (a) x; else if (a) x; else if (a) x; else if (a) x; else if (a) x; else if (a) x; else if (a) x; else if (a) x; else if (a) x; else if (a) x; else if (a) x; else if (a) x; else if (a) x; else if (a) x; else if (a) x; else if (a) x; else if (a) x; else if (a) x; else if (a) x; else if (a) x; else if (a) x; else if (a) x; else if (a) x; else if (a) x; else if (a) x; else if (a) x; else if (a) x; else if (a) x; else if (a) x; else if (a) x; else if (a) x; else if (a) x; else if (a) x; else if (a) x; else if (a) x; else if (a) x; else if (a) x; else if (a) x; else y; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (if_else (identifier a) (expression_statement (identifier x)) (if_else (identifier a) (expression_statement (identifier x)) (if_else (identifier a) (expression_statement (identifier x)) (if_else (identifier a) (expression_statement (identifier x)) (if_else (identifier a) (expression_statement (identifier x)) (if_else (identifier a) (expression_statement (identifier x)) (if_else (identifier a) (expression_statement (identifier x)) (if_else (identifier a) (expression_statement (identifier x)) (if_else (identifier a) (expression_statement (identifier x)) (if_else (identifier a) (expression_statement (identifier x)) (if_else (identifier a) (expression_statement (identifier x)) (if_else (identifier a) (expression_statement (identifier x)) (if_else (identifier a) (expression_statement (identifier x)) (if_else (identifier a) (expression_statement (identifier x)) (if_else (identifier a) (expression_statement (identifier x)) (if_else (identifier a) (expression_statement (identifier x)) (if_else (identifier a) (expression_statement (identifier x)) (if_else (identifier a) (expression_statement (identifier x)) (if_else (identifier a) (expression_statement (identifier x)) (if_else (identifier a) (expression_statement (identifier x)) (if_else (identifier a) (expression_statement (identifier x)) (if_else (identifier a) (expression_statement (identifier x)) (if_else (identifier a) (expression_statement (identifier x)) (if_else (identifier a) (expression_statement (identifier x)) (if_else (identifier a) (expression_statement (identifier x)) (if_else (identifier a) (expression_statement (identifier x)) (if_else (identifier a) (expression_statement (identifier x)) (if_else (identifier a) (expression_statement (identifier x)) (if_else (identifier a) (expression_statement (identifier x)) (if_else (identifier a) (expression_statement (identifier x)) (if_else (identifier a) (expression_statement (identifier x)) (if_else (identifier a) (expression_statement (identifier x)) (if_else (identifier a) (expression_statement (identifier x)) (if_else (identifier a) (expression_statement (identifier x)) (if_else (identifier a) (expression_statement (identifier x)) (if_else (identifier a) (expression_statement (identifier x)) (if_else (identifier a) (expression_statement (identifier x)) (if_else (identifier a) (expression_statement (identifier x)) (if_else (identifier a) (expression_statement (identifier x)) (if_else (identifier a) (expression_statement (identifier x)) (if_else (identifier a) (expression_statement (identifier x)) (if_else (identifier a) (expression_statement (identifier x)) (if_else (identifier a) (expression_statement (identifier x)) (if_else (identifier a) (expression_statement (identifier x)) (if_else (identifier a) (expression_statement (identifier x)) (if_else (identifier a) (expression_statement (identifier x)) (if_else (identifier a) (expression_statement (identifier x)) (if_else (identifier a) (expression_statement (identifier x)) (if_else (identifier a) (expression_statement (identifier x)) (if_else (identifier a) (expression_statement (identifier x)) (expression_statement (identifier y)))))))))))))))))))))))))))))))))))))))))))))))))))))))"));
        c_ast_test_expect(arguments, S8("void f(void) { x = ({ ({ ({ ({ ({ ({ ({ ({ ({ ({ ({ ({ ({ ({ ({ ({ ({ ({ ({ ({ ({ ({ ({ ({ ({ ({ ({ ({ ({ ({ 1; }); }); }); }); }); }); }); }); }); }); }); }); }); }); }); }); }); }); }); }); }); }); }); }); }); }); }); }); }); }); }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word void)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word void))))) (compound_statement (expression_statement (assign (identifier x) (statement_expression (compound_statement (expression_statement (statement_expression (compound_statement (expression_statement (statement_expression (compound_statement (expression_statement (statement_expression (compound_statement (expression_statement (statement_expression (compound_statement (expression_statement (statement_expression (compound_statement (expression_statement (statement_expression (compound_statement (expression_statement (statement_expression (compound_statement (expression_statement (statement_expression (compound_statement (expression_statement (statement_expression (compound_statement (expression_statement (statement_expression (compound_statement (expression_statement (statement_expression (compound_statement (expression_statement (statement_expression (compound_statement (expression_statement (statement_expression (compound_statement (expression_statement (statement_expression (compound_statement (expression_statement (statement_expression (compound_statement (expression_statement (statement_expression (compound_statement (expression_statement (statement_expression (compound_statement (expression_statement (statement_expression (compound_statement (expression_statement (statement_expression (compound_statement (expression_statement (statement_expression (compound_statement (expression_statement (statement_expression (compound_statement (expression_statement (statement_expression (compound_statement (expression_statement (statement_expression (compound_statement (expression_statement (statement_expression (compound_statement (expression_statement (statement_expression (compound_statement (expression_statement (statement_expression (compound_statement (expression_statement (statement_expression (compound_statement (expression_statement (statement_expression (compound_statement (expression_statement (statement_expression (compound_statement (expression_statement (number 1))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))"));
        c_ast_test_expect(arguments, S8("int (*(*(*(*(*(*(*(*(*(*(*(*(*(*(*(*(*(*(*(*(*(*(*(*(*(*(*(*(*(*f)(void))(void))(void))(void))(void))(void))(void))(void))(void))(void))(void))(void))(void))(void))(void))(void))(void))(void))(void))(void))(void))(void))(void))(void))(void))(void))(void))(void))(void))(void);"), S8("(translation_unit (declaration (decl_specifiers (specifier_word int)) (init_declarator (declarator_function (declarator_pointer (declarator_function (declarator_pointer (declarator_function (declarator_pointer (declarator_function (declarator_pointer (declarator_function (declarator_pointer (declarator_function (declarator_pointer (declarator_function (declarator_pointer (declarator_function (declarator_pointer (declarator_function (declarator_pointer (declarator_function (declarator_pointer (declarator_function (declarator_pointer (declarator_function (declarator_pointer (declarator_function (declarator_pointer (declarator_function (declarator_pointer (declarator_function (declarator_pointer (declarator_function (declarator_pointer (declarator_function (declarator_pointer (declarator_function (declarator_pointer (declarator_function (declarator_pointer (declarator_function (declarator_pointer (declarator_function (declarator_pointer (declarator_function (declarator_pointer (declarator_function (declarator_pointer (declarator_function (declarator_pointer (declarator_function (declarator_pointer (declarator_function (declarator_pointer (declarator_function (declarator_pointer (declarator_function (declarator_pointer (declarator_function (declarator_pointer (declarator_function (declarator_pointer (declarator_name f)) (parameter_list (parameter (decl_specifiers (specifier_word void)))))) (parameter_list (parameter (decl_specifiers (specifier_word void)))))) (parameter_list (parameter (decl_specifiers (specifier_word void)))))) (parameter_list (parameter (decl_specifiers (specifier_word void)))))) (parameter_list (parameter (decl_specifiers (specifier_word void)))))) (parameter_list (parameter (decl_specifiers (specifier_word void)))))) (parameter_list (parameter (decl_specifiers (specifier_word void)))))) (parameter_list (parameter (decl_specifiers (specifier_word void)))))) (parameter_list (parameter (decl_specifiers (specifier_word void)))))) (parameter_list (parameter (decl_specifiers (specifier_word void)))))) (parameter_list (parameter (decl_specifiers (specifier_word void)))))) (parameter_list (parameter (decl_specifiers (specifier_word void)))))) (parameter_list (parameter (decl_specifiers (specifier_word void)))))) (parameter_list (parameter (decl_specifiers (specifier_word void)))))) (parameter_list (parameter (decl_specifiers (specifier_word void)))))) (parameter_list (parameter (decl_specifiers (specifier_word void)))))) (parameter_list (parameter (decl_specifiers (specifier_word void)))))) (parameter_list (parameter (decl_specifiers (specifier_word void)))))) (parameter_list (parameter (decl_specifiers (specifier_word void)))))) (parameter_list (parameter (decl_specifiers (specifier_word void)))))) (parameter_list (parameter (decl_specifiers (specifier_word void)))))) (parameter_list (parameter (decl_specifiers (specifier_word void)))))) (parameter_list (parameter (decl_specifiers (specifier_word void)))))) (parameter_list (parameter (decl_specifiers (specifier_word void)))))) (parameter_list (parameter (decl_specifiers (specifier_word void)))))) (parameter_list (parameter (decl_specifiers (specifier_word void)))))) (parameter_list (parameter (decl_specifiers (specifier_word void)))))) (parameter_list (parameter (decl_specifiers (specifier_word void)))))) (parameter_list (parameter (decl_specifiers (specifier_word void)))))) (parameter_list (parameter (decl_specifiers (specifier_word void))))))))"));

        // small programs mixing the features
        c_ast_test_expect(arguments, S8("struct node { int value; struct node *next; }; static int sum(const struct node *n) { int total = 0; for (; n; n = n->next) total += n->value; return total; }"), S8("(translation_unit (declaration (decl_specifiers (struct_specifier (tag_name node) (member_list (member_declaration (decl_specifiers (specifier_word int)) (member_declarator (declarator_name value))) (member_declaration (decl_specifiers (struct_specifier (tag_name node))) (member_declarator (declarator_pointer (declarator_name next)))))))) (function_definition (decl_specifiers (specifier_word static) (specifier_word int)) (declarator_function (declarator_name sum) (parameter_list (parameter (decl_specifiers (specifier_word const) (struct_specifier (tag_name node))) (declarator_pointer (declarator_name n))))) (compound_statement (declaration (decl_specifiers (specifier_word int)) (init_declarator (declarator_name total) (number 0))) (for (identifier n) (assign (identifier n) (member_arrow next (identifier n))) (expression_statement (add_assign (identifier total) (member_arrow value (identifier n))))) (return (identifier total)))))"));
        c_ast_test_expect(arguments, S8("typedef struct { int x, y; } Point; typedef int (*Op)(Point, Point); static int add(Point a, Point b) { return a.x + b.x; } static const Op ops[] = { add };"), S8("(translation_unit (declaration (decl_specifiers (specifier_word typedef) (struct_specifier (member_list (member_declaration (decl_specifiers (specifier_word int)) (member_declarator (declarator_name x)) (member_declarator (declarator_name y)))))) (init_declarator (declarator_name Point))) (declaration (decl_specifiers (specifier_word typedef) (specifier_word int)) (init_declarator (declarator_function (declarator_pointer (declarator_name Op)) (parameter_list (parameter (decl_specifiers (typedef_name Point))) (parameter (decl_specifiers (typedef_name Point))))))) (function_definition (decl_specifiers (specifier_word static) (specifier_word int)) (declarator_function (declarator_name add) (parameter_list (parameter (decl_specifiers (typedef_name Point)) (declarator_name a)) (parameter (decl_specifiers (typedef_name Point)) (declarator_name b)))) (compound_statement (return (add (member x (identifier a)) (member x (identifier b)))))) (declaration (decl_specifiers (specifier_word static) (specifier_word const) (typedef_name Op)) (init_declarator (declarator_array (declarator_name ops)) (initializer_list (identifier add)))))"));
        c_ast_test_expect(arguments, S8("int f(int n) { int r = 0; switch (n) { case 0: r = 1; break; default: while (n--) r *= 2; } return r; }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word int)) (declarator_function (declarator_name f) (parameter_list (parameter (decl_specifiers (specifier_word int)) (declarator_name n)))) (compound_statement (declaration (decl_specifiers (specifier_word int)) (init_declarator (declarator_name r) (number 0))) (switch (identifier n) (compound_statement (case (number 0) (expression_statement (assign (identifier r) (number 1)))) (break) (default (while (post_decrement (identifier n)) (expression_statement (multiply_assign (identifier r) (number 2))))))) (return (identifier r)))))"));
        c_ast_test_expect(arguments, S8("static inline __attribute__((always_inline)) int g(int *p) { return ({ int t = *p; t + 1; }); }"), S8("(translation_unit (function_definition (decl_specifiers (specifier_word static) (specifier_word inline) (attribute_list (attribute_gnu (attribute always_inline))) (specifier_word int)) (declarator_function (declarator_name g) (parameter_list (parameter (decl_specifiers (specifier_word int)) (declarator_pointer (declarator_name p))))) (compound_statement (return (statement_expression (compound_statement (declaration (decl_specifiers (specifier_word int)) (init_declarator (declarator_name t) (dereference (identifier p)))) (expression_statement (add (identifier t) (number 1)))))))))"));
        c_ast_test_expect(arguments, S8("enum color { RED, GREEN = 5, BLUE }; const char *names[] = { [RED] = \"red\", [GREEN] = \"green\", [BLUE] = \"blue\" };"), S8("(translation_unit (declaration (decl_specifiers (enum_specifier (tag_name color) (enumerator_list (enumerator RED) (enumerator GREEN (number 5)) (enumerator BLUE))))) (declaration (decl_specifiers (specifier_word const) (specifier_word char)) (init_declarator (declarator_pointer (declarator_array (declarator_name names))) (initializer_list (designation (designator_index (identifier RED)) (string \"red\")) (designation (designator_index (identifier GREEN)) (string \"green\")) (designation (designator_index (identifier BLUE)) (string \"blue\"))))))"));
        c_ast_test_expect(arguments, S8("typedef unsigned long size_t; void *memset(void *s, int c, size_t n); size_t strlen(const char *s) { size_t n = 0; while (s[n]) n++; return n; }"), S8("(translation_unit (declaration (decl_specifiers (specifier_word typedef) (specifier_word unsigned) (specifier_word long)) (init_declarator (declarator_name size_t))) (declaration (decl_specifiers (specifier_word void)) (init_declarator (declarator_pointer (declarator_function (declarator_name memset) (parameter_list (parameter (decl_specifiers (specifier_word void)) (declarator_pointer (declarator_name s))) (parameter (decl_specifiers (specifier_word int)) (declarator_name c)) (parameter (decl_specifiers (typedef_name size_t)) (declarator_name n))))))) (function_definition (decl_specifiers (typedef_name size_t)) (declarator_function (declarator_name strlen) (parameter_list (parameter (decl_specifiers (specifier_word const) (specifier_word char)) (declarator_pointer (declarator_name s))))) (compound_statement (declaration (decl_specifiers (typedef_name size_t)) (init_declarator (declarator_name n) (number 0))) (while (index (identifier s) (identifier n)) (expression_statement (post_increment (identifier n)))) (return (identifier n)))))"));

        // negative: truncated input and missing terminators
        c_ast_test_expect_failure(arguments, S8("int"));
        c_ast_test_expect_failure(arguments, S8("int x"));
        c_ast_test_expect_failure(arguments, S8("int x ="));
        c_ast_test_expect_failure(arguments, S8("int x = ;"));
        c_ast_test_expect_failure(arguments, S8("int x, ;"));
        c_ast_test_expect_failure(arguments, S8("int , x;"));
        c_ast_test_expect_failure(arguments, S8("int x = 1 2;"));
        c_ast_test_expect_failure(arguments, S8("int 3x;"));
        c_ast_test_expect_failure(arguments, S8("int a[3;"));
        c_ast_test_expect_failure(arguments, S8("int (x;"));
        c_ast_test_expect_failure(arguments, S8("int f("));
        c_ast_test_expect_failure(arguments, S8("int f(int a,) { }"));
        c_ast_test_expect_failure(arguments, S8("int f( { }"));
        c_ast_test_expect_failure(arguments, S8("int f(void) { return 1 }"));
        c_ast_test_expect_failure(arguments, S8("int f(void) { return 0; } }"));
        c_ast_test_expect_failure(arguments, S8("void f(void) {"));
        c_ast_test_expect_failure(arguments, S8("}"));
        c_ast_test_expect_failure(arguments, S8(")"));
        c_ast_test_expect_failure(arguments, S8("struct S {"));
        c_ast_test_expect_failure(arguments, S8("struct { int a } s;"));
        c_ast_test_expect_failure(arguments, S8("struct S { int a : ; };"));
        c_ast_test_expect_failure(arguments, S8("enum { A B };"));
        c_ast_test_expect_failure(arguments, S8("enum { A,, B };"));
        c_ast_test_expect_failure(arguments, S8("enum E { A"));
        c_ast_test_expect_failure(arguments, S8("int x = {1, 2;"));
        c_ast_test_expect_failure(arguments, S8("int x = { .a };"));
        c_ast_test_expect_failure(arguments, S8("int x = { [1 };"));
        c_ast_test_expect_failure(arguments, S8("int x = }"));
        c_ast_test_expect_failure(arguments, S8("int [3] x;"));
        c_ast_test_expect_failure(arguments, S8("int *;"));
        c_ast_test_expect_failure(arguments, S8("int (*)(void) x;"));
        c_ast_test_expect_failure(arguments, S8("_Static_assert(1, 2);"));
        c_ast_test_expect_failure(arguments, S8("_Static_assert(1, \"m\")"));
        c_ast_test_expect_failure(arguments, S8("x y z;"));
        c_ast_test_expect_failure(arguments, S8("asm(x);"));
        c_ast_test_expect_failure(arguments, S8("asm(\"nop\") int x;"));
        c_ast_test_expect_failure(arguments, S8("int x asm();"));
        c_ast_test_expect_failure(arguments, S8("__attribute__((unused) int x;"));
        c_ast_test_expect_failure(arguments, S8("__attribute__(unused) int x;"));
        c_ast_test_expect_failure(arguments, S8("[[unused] int x;"));
        c_ast_test_expect_failure(arguments, S8("[[gnu:unused]] int x;"));

        // negative: attribute lists around declarators
        c_ast_test_expect_failure(arguments, S8("int (__attribute__((x)));"));
        c_ast_test_expect_failure(arguments, S8("void g(int (__attribute__((x))));"));
        c_ast_test_expect_failure(arguments, S8("int s = sizeof(int (__attribute__((x))));"));
        c_ast_test_expect_failure(arguments, S8("int s = sizeof(int (__attribute__((x)) p));"));
        c_ast_test_expect_failure(arguments, S8("int (__attribute__((x)) const p);"));
        c_ast_test_expect_failure(arguments, S8("int (__attribute__((x)) p;"));
        c_ast_test_expect_failure(arguments, S8("int (__attribute__((x)) p));"));
        c_ast_test_expect_failure(arguments, S8("int (__attribute__((x) p);"));
        c_ast_test_expect_failure(arguments, S8("int a, __attribute__((x)) ;"));
        c_ast_test_expect_failure(arguments, S8("int a, __attribute__((x)) b __attribute__((y)) __asm__(\"q\") __attribute__((z));"));
        c_ast_test_expect_failure(arguments, S8("struct S { int a, __attribute__((x)) ; };"));
        c_ast_test_expect_failure(arguments, S8("struct S { int (__attribute__((x))); };"));
        c_ast_test_expect_failure(arguments, S8("typedef int T; int (__attribute__((x)) f(int T)) { T x; }"));

        // negative: statements and expressions in function bodies
        c_ast_test_expect_failure(arguments, S8("void f(void) { a + ; }"));
        c_ast_test_expect_failure(arguments, S8("void f(void) { x = (int; }"));
        c_ast_test_expect_failure(arguments, S8("void f(void) { x = (a + b; }"));
        c_ast_test_expect_failure(arguments, S8("void f(void) { a + b); }"));
        c_ast_test_expect_failure(arguments, S8("void f(void) { return return; }"));
        c_ast_test_expect_failure(arguments, S8("void f(void) { return 1 }"));
        c_ast_test_expect_failure(arguments, S8("void f(void) { x = ; }"));
        c_ast_test_expect_failure(arguments, S8("void f(void) { x = y z; }"));
        c_ast_test_expect_failure(arguments, S8("void f(void) { a = = b; }"));
        c_ast_test_expect_failure(arguments, S8("void f(void) { a[]; }"));
        c_ast_test_expect_failure(arguments, S8("void f(void) { f(1,); }"));
        c_ast_test_expect_failure(arguments, S8("void f(void) { f(,1); }"));
        c_ast_test_expect_failure(arguments, S8("void f(void) { f(1 2); }"));
        c_ast_test_expect_failure(arguments, S8("void f(void) { sizeof(); }"));
        c_ast_test_expect_failure(arguments, S8("void f(void) { sizeof(int; }"));
        c_ast_test_expect_failure(arguments, S8("void f(void) { a.1; }"));
        c_ast_test_expect_failure(arguments, S8("void f(void) { a ? b ; }"));
        c_ast_test_expect_failure(arguments, S8("void f(void) { (int)x = ; }"));
        c_ast_test_expect_failure(arguments, S8("void f(void) { x = ({ 1; ); }"));
        c_ast_test_expect_failure(arguments, S8("void f(void) { x = ({ 1 }); }"));
        c_ast_test_expect_failure(arguments, S8("void f(void) { x = _Generic(y, int 1); }"));
        c_ast_test_expect_failure(arguments, S8("void f(void) { x = _Generic(y, int: ); }"));
        c_ast_test_expect_failure(arguments, S8("void f(void) { x = &&; }"));
        c_ast_test_expect_failure(arguments, S8("void f(void) { goto ; }"));
        c_ast_test_expect_failure(arguments, S8("void f(void) { case: ; }"));
        c_ast_test_expect_failure(arguments, S8("void f(void) { switch (a) { case : ; } }"));
        c_ast_test_expect_failure(arguments, S8("void f(void) { switch (a) { default ; } }"));
        c_ast_test_expect_failure(arguments, S8("void f(void) { if a b; }"));
        c_ast_test_expect_failure(arguments, S8("void f(void) { if (a) else b; }"));
        c_ast_test_expect_failure(arguments, S8("void f(void) { else x; }"));
        c_ast_test_expect_failure(arguments, S8("void f(void) { do x; while (a) }"));
        c_ast_test_expect_failure(arguments, S8("void f(void) { do x; (a); }"));
        c_ast_test_expect_failure(arguments, S8("void f(void) { do while (1); }"));
        c_ast_test_expect_failure(arguments, S8("void f(void) { for (;) ; }"));
        c_ast_test_expect_failure(arguments, S8("void f(void) { for (;;) }"));
        c_ast_test_expect_failure(arguments, S8("void f(void) { for (int i = 0 i < 3; i++) ; }"));
        c_ast_test_expect_failure(arguments, S8("void f(void) { int a, ; }"));
        c_ast_test_expect_failure(arguments, S8("void f(void) { int a = ; }"));
        c_ast_test_expect_failure(arguments, S8("void f(void) { a b; }"));
        c_ast_test_expect_failure(arguments, S8("void f(void) { asm(\"nop\" : \"=r\"(x) }"));
        c_ast_test_expect_failure(arguments, S8("void f(void) { asm(\"\" : : : : : ); }"));
        c_ast_test_expect_failure(arguments, S8("void f(void) { asm(\"\" : \"=r\" x); }"));
        c_ast_test_expect_failure(arguments, S8("void f(void) { asm(\"\" : \"=r\"(x) \"r\"(y)); }"));
        c_ast_test_expect_failure(arguments, S8("void f(void) { asm(\"\" : [ \"r\"(x)); }"));
        c_ast_test_expect_failure(arguments, S8("void f(void) { asm(1); }"));
        c_ast_test_expect_failure(arguments, S8("void f(void) { { { } }"));
        c_ast_test_expect_failure(arguments, S8("void f(void) { x = sizeof int; }"));

        // negative: typedef-name resolution
        c_ast_test_expect_failure(arguments, S8("typedef int T; void f(void) { (x)y; }"));
        c_ast_test_expect_failure(arguments, S8("typedef int T; void f(void) { int T; T x; }"));
        c_ast_test_expect_failure(arguments, S8("typedef int T; void f(int T) { T x; }"));
        c_ast_test_expect_failure(arguments, S8("typedef int T; void f(int T, T x);"));
        c_ast_test_expect_failure(arguments, S8("typedef int T; void f(void) { enum { T }; T x; }"));
        c_ast_test_expect_failure(arguments, S8("typedef int T; void f(void) { unsigned T; T x; }"));
        c_ast_test_expect_failure(arguments, S8("typedef int T; void f(void) { for (int T = 0; ; ) T x; }"));
        c_ast_test_expect_failure(arguments, S8("void f(void) { typedef int T; } void g(void) { T x; }"));
        c_ast_test_expect_failure(arguments, S8("void f(void) { { typedef int T; } T x; }"));
        c_ast_test_expect_failure(arguments, S8("typedef int T; void f(void) { x = T; }"));
        c_ast_test_expect_failure(arguments, S8("typedef int T; void f(void) { sizeof T; }"));
        c_ast_test_expect_failure(arguments, S8("typedef int T; void f(void) { T = 1; }"));
        c_ast_test_expect_failure(arguments, S8("typedef int T; void f(void) { int T = sizeof(T x); }"));
    return result;
}
#undef c_ast_oracle_expect_dialect

// The tree expression typer (c_ast_types.c): one range of a function body per
// case, answered on a private machine from a tree built over the source, with
// and without constraint checks. Each case states the C type the range has by
// the standard (size_t and ptrdiff_t by the native target's data model, LP64 or
// LLP64); an accepted kind answers it,
// a shape whose machine answer would append a type row declines, and a range
// that is no expression node misses. The corpus half of the contract (every
// tree answer equals the machine's, and no diagnostic changes) is
// c_ast_corpus_types.
typedef struct CAstTypeCase CAstTypeCase;
struct CAstTypeCase
{
    String8 source;
    // The range: `count` tokens from the `occurrence`th (0-based) token spelled
    // `first`.
    String8 first;
    u32 occurrence;
    u32 count;
    u32 status;
    CTypeKind kind;
    // Answered only without constraint checks: with them the machine types an
    // operand the tree does not, so the query declines.
    bool unchecked_only;
    // The type under the LLP64 data model, where size_t and ptrdiff_t are the
    // long long kinds; C_TYPE_INVALID when it is `kind` in both.
    CTypeKind llp64_kind;
    // With constraint checks the answer replays the machine's typing of a
    // cast's string-literal operand.
    bool replayed;
    // The range is one string-literal token, perhaps parenthesized: with or
    // without constraint checks the answer is the row its replay appends, so
    // the probe, which does not replay, reports no type.
    bool replay_answer;
};

BUSTER_GLOBAL_LOCAL CAstTypeCase const c_ast_type_cases[] = {
    {S8_INITIALIZER("int g; int f(void) { return g; }"), S8_INITIALIZER("g"), 1, 1, C_TEST_AST_TYPE_PROBE_ANSWER, C_TYPE_INT},
    {S8_INITIALIZER("int g; int f(void) { return ((g)); }"), S8_INITIALIZER("("), 1, 5, C_TEST_AST_TYPE_PROBE_ANSWER, C_TYPE_INT},
    {S8_INITIALIZER("int x; long f(void) { long x = 1; return x; }"), S8_INITIALIZER("x"), 2, 1, C_TEST_AST_TYPE_PROBE_ANSWER, C_TYPE_LONG},
    {S8_INITIALIZER("long f(void) { return 7L; }"), S8_INITIALIZER("7L"), 0, 1, C_TEST_AST_TYPE_PROBE_ANSWER, C_TYPE_LONG},
    {S8_INITIALIZER("double f(void) { return 1.5; }"), S8_INITIALIZER("1.5"), 0, 1, C_TEST_AST_TYPE_PROBE_ANSWER, C_TYPE_DOUBLE},
    {S8_INITIALIZER("int f(void) { return 'a'; }"), S8_INITIALIZER("'a'"), 0, 1, C_TEST_AST_TYPE_PROBE_ANSWER, C_TYPE_INT},
    {S8_INITIALIZER("enum E { RED, GREEN }; int f(void) { return GREEN; }"), S8_INITIALIZER("GREEN"), 1, 1, C_TEST_AST_TYPE_PROBE_ANSWER, C_TYPE_INT},
    {S8_INITIALIZER("struct S { short member; }; short f(struct S value) { return value.member; }"), S8_INITIALIZER("value"), 1, 3,
     C_TEST_AST_TYPE_PROBE_ANSWER, C_TYPE_SHORT},
    {S8_INITIALIZER("struct S { char* name; }; char* f(struct S* pointer) { return pointer->name; }"), S8_INITIALIZER("pointer"), 1, 3,
     C_TEST_AST_TYPE_PROBE_ANSWER, C_TYPE_POINTER},
    {S8_INITIALIZER("struct S { struct { int inner; }; }; int f(struct S* pointer) { return pointer->inner; }"), S8_INITIALIZER("pointer"), 1, 3,
     C_TEST_AST_TYPE_PROBE_ANSWER, C_TYPE_INT},
    // A pass remembers a member found directly in its aggregate
    // (c_ast_types_member_lookup): the second `p->bits` keeps the bit-field
    // width that promotes it to int, and `b->x` is a different aggregate's
    // member of the same name. A promoted member is looked up again each time.
    {S8_INITIALIZER("struct S { unsigned bits : 3; }; int f(struct S* p) { return p->bits + p->bits; }"), S8_INITIALIZER("p"), 1, 7,
     C_TEST_AST_TYPE_PROBE_ANSWER, C_TYPE_INT},
    {S8_INITIALIZER("struct A { char x; }; struct B { long x; }; long f(struct A* a, struct B* b) { return a->x + b->x; }"), S8_INITIALIZER("a"), 1,
     7, C_TEST_AST_TYPE_PROBE_ANSWER, C_TYPE_LONG},
    {S8_INITIALIZER("struct S { struct { long inner; }; }; long f(struct S* p) { return p->inner + p->inner; }"), S8_INITIALIZER("p"), 1, 7,
     C_TEST_AST_TYPE_PROBE_ANSWER, C_TYPE_LONG},
    // A pass remembers the usual arithmetic conversions (c_ast_types_arithmetic)
    // by both operand rows and both bit-field widths: the same typedef row
    // gives unsigned int without a width and int with a narrow one, and the
    // right operand's row is part of the key.
    {S8_INITIALIZER("typedef unsigned U; struct S { U bits : 3; U whole; }; int f(struct S* p) { return (p->whole + p->whole) + (p->bits + p->bits); }"),
     S8_INITIALIZER("p"), 3, 7, C_TEST_AST_TYPE_PROBE_ANSWER, C_TYPE_INT},
    {S8_INITIALIZER("typedef unsigned U; struct S { U bits : 3; U whole; }; U f(struct S* p) { return (p->bits + p->bits) + (p->whole + p->whole); }"),
     S8_INITIALIZER("p"), 3, 7, C_TEST_AST_TYPE_PROBE_ANSWER, C_TYPE_UNSIGNED_INT},
    {S8_INITIALIZER("long f(int a, long b) { return (a + a) + (a + b); }"), S8_INITIALIZER("a"), 3, 3, C_TEST_AST_TYPE_PROBE_ANSWER, C_TYPE_LONG},
    {S8_INITIALIZER("int f(int* items) { return items[2]; }"), S8_INITIALIZER("items"), 1, 4, C_TEST_AST_TYPE_PROBE_ANSWER, C_TYPE_INT},
    {S8_INITIALIZER("unsigned char table[4]; unsigned char f(int i) { return table[i]; }"), S8_INITIALIZER("table"), 1, 4, C_TEST_AST_TYPE_PROBE_ANSWER,
     C_TYPE_UNSIGNED_CHAR},
    {S8_INITIALIZER("double f(double* slot) { return *slot; }"), S8_INITIALIZER("*"), 1, 2, C_TEST_AST_TYPE_PROBE_ANSWER, C_TYPE_DOUBLE},
    {S8_INITIALIZER("float h(int); float f(void) { return h(1); }"), S8_INITIALIZER("h"), 1, 4, C_TEST_AST_TYPE_PROBE_ANSWER, C_TYPE_FLOAT},
    {S8_INITIALIZER("float (*hp)(int); float f(void) { return hp(1); }"), S8_INITIALIZER("hp"), 1, 4, C_TEST_AST_TYPE_PROBE_ANSWER, C_TYPE_FLOAT},
    {S8_INITIALIZER("struct S { int a; }; int f(struct S* p) { return p->a + 1; }"), S8_INITIALIZER("p"), 1, 3, C_TEST_AST_TYPE_PROBE_ANSWER, C_TYPE_INT},
    // Stage 2: operators over operands the tree already typed.
    {S8_INITIALIZER("int f(int a, int b) { return a + b; }"), S8_INITIALIZER("a"), 1, 3, C_TEST_AST_TYPE_PROBE_ANSWER, C_TYPE_INT},
    {S8_INITIALIZER("long f(long a, int b) { return a * b; }"), S8_INITIALIZER("a"), 1, 3, C_TEST_AST_TYPE_PROBE_ANSWER, C_TYPE_LONG},
    {S8_INITIALIZER("double f(int a, double b) { return a / b; }"), S8_INITIALIZER("a"), 1, 3, C_TEST_AST_TYPE_PROBE_ANSWER, C_TYPE_DOUBLE},
    {S8_INITIALIZER("unsigned f(unsigned a, int b) { return a % b; }"), S8_INITIALIZER("a"), 1, 3, C_TEST_AST_TYPE_PROBE_ANSWER, C_TYPE_UNSIGNED_INT},
    {S8_INITIALIZER("int f(char c) { return c << 1; }"), S8_INITIALIZER("c"), 1, 3, C_TEST_AST_TYPE_PROBE_ANSWER, C_TYPE_INT},
    {S8_INITIALIZER("unsigned long long f(unsigned long long a, int s) { return a >> s; }"), S8_INITIALIZER("a"), 1, 3, C_TEST_AST_TYPE_PROBE_ANSWER,
     C_TYPE_UNSIGNED_LONG_LONG},
    {S8_INITIALIZER("int f(unsigned char c) { return -c; }"), S8_INITIALIZER("-"), 0, 2, C_TEST_AST_TYPE_PROBE_ANSWER, C_TYPE_INT},
    {S8_INITIALIZER("unsigned f(unsigned a) { return ~a; }"), S8_INITIALIZER("~"), 0, 2, C_TEST_AST_TYPE_PROBE_ANSWER, C_TYPE_UNSIGNED_INT},
    {S8_INITIALIZER("float f(float a) { return +a; }"), S8_INITIALIZER("+"), 0, 2, C_TEST_AST_TYPE_PROBE_ANSWER, C_TYPE_FLOAT},
    {S8_INITIALIZER("int f(double d) { return !d; }"), S8_INITIALIZER("!"), 0, 2, C_TEST_AST_TYPE_PROBE_ANSWER, C_TYPE_INT},
    // A narrow unsigned bit-field promotes to int.
    {S8_INITIALIZER("struct S { unsigned bits : 3; }; int f(struct S s) { return s.bits + 1u; }"), S8_INITIALIZER("s"), 1, 5,
     C_TEST_AST_TYPE_PROBE_ANSWER, C_TYPE_UNSIGNED_INT},
    {S8_INITIALIZER("struct S { unsigned bits : 3; }; int f(struct S s) { return s.bits + 1; }"), S8_INITIALIZER("s"), 1, 5,
     C_TEST_AST_TYPE_PROBE_ANSWER, C_TYPE_INT},
    // An operand that ends in `. name` without being a member node: the
    // machine re-types the tokens before the final `.` for a bit-field width
    // (c_ast_types_operand_width). On `a + s`, `c ? a : s` and `a + s` under
    // a unary operator that probe fails without a new row, so the width is 0
    // and `unsigned` stays unsigned; a width of 3 would promote it to int.
    {S8_INITIALIZER("struct S { unsigned bits : 3; }; unsigned f(struct S s, unsigned a) { return (a + s.bits) << 1; }"), S8_INITIALIZER("("), 1, 9,
     C_TEST_AST_TYPE_PROBE_ANSWER, C_TYPE_UNSIGNED_INT},
    {S8_INITIALIZER("struct S { unsigned bits : 3; }; unsigned f(struct S s, unsigned a) { return -(a + s.bits); }"), S8_INITIALIZER("-"), 0, 8,
     C_TEST_AST_TYPE_PROBE_ANSWER, C_TYPE_UNSIGNED_INT},
    {S8_INITIALIZER("struct S { unsigned bits : 3; }; unsigned f(struct S s, int c, unsigned a) { return (c ? a : s.bits) + 1u; }"),
     S8_INITIALIZER("("), 1, 11, C_TEST_AST_TYPE_PROBE_ANSWER, C_TYPE_UNSIGNED_INT},
    // On `t.n + s` the probe walks `t.n`, an accepted member over an
    // unqualified aggregate, and fails at `+` without a new row.
    {S8_INITIALIZER("struct S { unsigned bits : 3; unsigned n; }; unsigned f(struct S s, struct S t) { return (t.n + s.bits) << 1; }"),
     S8_INITIALIZER("("), 1, 11, C_TEST_AST_TYPE_PROBE_ANSWER, C_TYPE_UNSIGNED_INT},
    // On `t->n + s` over a const aggregate the probe's member step reads the
    // member's shared qualified copy (c_parse_intern_qualified_type), which
    // the accepted `t->n` node vouches for, and appends nothing before `+`
    // fails it.
    {S8_INITIALIZER("struct S { unsigned bits : 3; unsigned n; }; unsigned f(struct S s, struct S const* t) { return (t->n + s.bits) << 1; }"),
     S8_INITIALIZER("("), 1, 11, C_TEST_AST_TYPE_PROBE_ANSWER, C_TYPE_UNSIGNED_INT},
    // The probe on `x, s` succeeds through the comma and reads width 3, and
    // on `t->n + s` with an aligned member type the member step appends a
    // qualified copy that may not be shared before `+` fails it: both stay
    // with the machine.
    {S8_INITIALIZER("struct S { unsigned bits : 3; }; int f(struct S s, int x) { return (x, s.bits) << 1; }"), S8_INITIALIZER("("), 1, 9,
     C_TEST_AST_TYPE_PROBE_DECLINE, C_TYPE_INVALID},
    {S8_INITIALIZER("typedef unsigned A16 __attribute__((aligned(16))); struct S { unsigned bits : 3; A16 n; }; "
                    "unsigned f(struct S s, struct S const* t) { return (t->n + s.bits) << 1; }"),
     S8_INITIALIZER("("), 4, 11, C_TEST_AST_TYPE_PROBE_DECLINE, C_TYPE_INVALID},
    {S8_INITIALIZER("int f(long a, long b) { return a < b; }"), S8_INITIALIZER("a"), 1, 3, C_TEST_AST_TYPE_PROBE_ANSWER, C_TYPE_INT},
    {S8_INITIALIZER("int f(int* p) { return p != 0; }"), S8_INITIALIZER("p"), 1, 3, C_TEST_AST_TYPE_PROBE_ANSWER, C_TYPE_INT},
    {S8_INITIALIZER("int f(int* p, int n) { return p && n; }"), S8_INITIALIZER("p"), 1, 3, C_TEST_AST_TYPE_PROBE_ANSWER, C_TYPE_INT},
    {S8_INITIALIZER("int f(int a, int b) { return a || b; }"), S8_INITIALIZER("a"), 1, 3, C_TEST_AST_TYPE_PROBE_ANSWER, C_TYPE_INT},
    {S8_INITIALIZER("char* f(char* p, int n) { return p + n; }"), S8_INITIALIZER("p"), 1, 3, C_TEST_AST_TYPE_PROBE_ANSWER, C_TYPE_POINTER},
    {S8_INITIALIZER("int* f(int* p) { return 1 + p; }"), S8_INITIALIZER("1"), 0, 3, C_TEST_AST_TYPE_PROBE_ANSWER, C_TYPE_POINTER},
    {S8_INITIALIZER("int f(int* p, int i) { return *(p + i); }"), S8_INITIALIZER("*"), 1, 6, C_TEST_AST_TYPE_PROBE_ANSWER, C_TYPE_INT},
    {S8_INITIALIZER("int f(int* p, int i) { return p[i + 1]; }"), S8_INITIALIZER("p"), 1, 6, C_TEST_AST_TYPE_PROBE_ANSWER, C_TYPE_INT},
    {S8_INITIALIZER("short f(short a, int b) { return a = b; }"), S8_INITIALIZER("a"), 1, 3, C_TEST_AST_TYPE_PROBE_ANSWER, C_TYPE_SHORT},
    {S8_INITIALIZER("long f(long a) { return a += 2; }"), S8_INITIALIZER("a"), 1, 3, C_TEST_AST_TYPE_PROBE_ANSWER, C_TYPE_LONG},
    // `++` and `--` keep the operand's own row, unpromoted and pointers
    // included; checked, the operand must be a modifiable place. An array
    // parameter is the pointer it is adjusted to; only its declaration keeps
    // the array row for bound/VLA ownership.
    {S8_INITIALIZER("int f(int i) { return i++; }"), S8_INITIALIZER("i"), 1, 2, C_TEST_AST_TYPE_PROBE_ANSWER, C_TYPE_INT},
    {S8_INITIALIZER("long f(long i) { return --i; }"), S8_INITIALIZER("--"), 0, 2, C_TEST_AST_TYPE_PROBE_ANSWER, C_TYPE_LONG},
    {S8_INITIALIZER("unsigned char f(unsigned char c) { return ++c; }"), S8_INITIALIZER("++"), 0, 2, C_TEST_AST_TYPE_PROBE_ANSWER,
     C_TYPE_UNSIGNED_CHAR},
    {S8_INITIALIZER("char* f(char* p) { return (p)--; }"), S8_INITIALIZER("("), 1, 4, C_TEST_AST_TYPE_PROBE_ANSWER, C_TYPE_POINTER},
    {S8_INITIALIZER("struct S { short n; }; short f(struct S* p) { return p->n++; }"), S8_INITIALIZER("p"), 1, 4, C_TEST_AST_TYPE_PROBE_ANSWER,
     C_TYPE_SHORT},
    {S8_INITIALIZER("int f(int* a, int i) { return a[i++]; }"), S8_INITIALIZER("a"), 1, 5, C_TEST_AST_TYPE_PROBE_ANSWER, C_TYPE_INT},
    {S8_INITIALIZER("int f(int* p) { return *p++ + 1; }"), S8_INITIALIZER("*"), 1, 5, C_TEST_AST_TYPE_PROBE_ANSWER, C_TYPE_INT},
    {S8_INITIALIZER("void f(int a[4]) { a++; }"), S8_INITIALIZER("a"), 1, 2, C_TEST_AST_TYPE_PROBE_ANSWER, C_TYPE_POINTER},
    {S8_INITIALIZER("void consume(char***); void f(char* a[]) { consume(&a); }"), S8_INITIALIZER("a"), 1, 1,
     C_TEST_AST_TYPE_PROBE_ANSWER, C_TYPE_POINTER},
    {S8_INITIALIZER("void consume(char***); void f(char* a[]) { consume(&a); }"), S8_INITIALIZER("&"), 0, 2,
     C_TEST_AST_TYPE_PROBE_ANSWER, C_TYPE_POINTER},
    // A return query has already interned both adjusted pointer rows. The
    // read-only tree lookup must reuse them without appending a duplicate.
    {S8_INITIALIZER("char*** f(char* a[]) { return &a; }"), S8_INITIALIZER("&"), 0, 2,
     C_TEST_AST_TYPE_PROBE_ANSWER, C_TYPE_POINTER},
    {S8_INITIALIZER("void f(void) { int a[4]; (void)a; }"), S8_INITIALIZER("a"), 1, 1, C_TEST_AST_TYPE_PROBE_ANSWER, C_TYPE_ARRAY},
    // Over a qualified aggregate's member, which the pass now types through
    // its shared qualified copy, and over an element of a qualified array
    // member, which it still declines.
    {S8_INITIALIZER("struct S { int n; }; void f(struct S volatile* p) { p->n++; }"), S8_INITIALIZER("p"), 1, 4, C_TEST_AST_TYPE_PROBE_ANSWER,
     C_TYPE_INT},
    {S8_INITIALIZER("struct S { int v[4]; }; void f(struct S volatile* p) { p->v[1]++; }"), S8_INITIALIZER("p"), 1, 7, C_TEST_AST_TYPE_PROBE_DECLINE,
     C_TYPE_INVALID},
    {S8_INITIALIZER("int f(long a, int b) { return (a, b); }"), S8_INITIALIZER("a"), 1, 3, C_TEST_AST_TYPE_PROBE_ANSWER, C_TYPE_INT},
    {S8_INITIALIZER("double f(int c, int a, double b) { return c ? a : b; }"), S8_INITIALIZER("c"), 1, 5, C_TEST_AST_TYPE_PROBE_ANSWER, C_TYPE_DOUBLE},
    {S8_INITIALIZER("long f(long a, long b) { return a ?: b; }"), S8_INITIALIZER("a"), 1, 4, C_TEST_AST_TYPE_PROBE_ANSWER, C_TYPE_LONG},
    {S8_INITIALIZER("unsigned long f(int a) { return sizeof a; }"), S8_INITIALIZER("sizeof"), 0, 2, C_TEST_AST_TYPE_PROBE_ANSWER, C_TYPE_UNSIGNED_LONG,
     false, C_TYPE_UNSIGNED_LONG_LONG},
    {S8_INITIALIZER("unsigned long f(void) { return sizeof(int); }"), S8_INITIALIZER("sizeof"), 0, 4, C_TEST_AST_TYPE_PROBE_ANSWER, C_TYPE_UNSIGNED_LONG,
     false, C_TYPE_UNSIGNED_LONG_LONG},
    {S8_INITIALIZER("unsigned long f(void) { return _Alignof(double); }"), S8_INITIALIZER("_Alignof"), 0, 4, C_TEST_AST_TYPE_PROBE_ANSWER,
     C_TYPE_UNSIGNED_LONG, false, C_TYPE_UNSIGNED_LONG_LONG},
    {S8_INITIALIZER("typedef unsigned short U16; U16 f(int a) { return (U16)a; }"), S8_INITIALIZER("("), 1, 4, C_TEST_AST_TYPE_PROBE_ANSWER,
     C_TYPE_UNSIGNED_SHORT},
    {S8_INITIALIZER("typedef struct P { int x; } P; P f(void) { return (P){1}; }"), S8_INITIALIZER("("), 1, 6, C_TEST_AST_TYPE_PROBE_ANSWER,
     C_TYPE_STRUCT},
    // Row-appending shapes, answered once the body queries have interned
    // their rows: `&` and a cast to primitive specifier words or a typedef
    // name under plain `*`s (CTypeInterning).
    {S8_INITIALIZER("int* f(int a) { return &a; }"), S8_INITIALIZER("&"), 0, 2, C_TEST_AST_TYPE_PROBE_ANSWER, C_TYPE_POINTER},
    {S8_INITIALIZER("struct S { int a; }; int* f(struct S* p) { return &p->a; }"), S8_INITIALIZER("&"), 0, 4, C_TEST_AST_TYPE_PROBE_ANSWER,
     C_TYPE_POINTER},
    {S8_INITIALIZER("int f(int* p) { return *&p[1]; }"), S8_INITIALIZER("*"), 1, 6, C_TEST_AST_TYPE_PROBE_ANSWER, C_TYPE_INT},
    {S8_INITIALIZER("long f(int a) { return (long)a; }"), S8_INITIALIZER("("), 1, 4, C_TEST_AST_TYPE_PROBE_ANSWER, C_TYPE_LONG},
    {S8_INITIALIZER("unsigned char f(int a) { return (unsigned char)a; }"), S8_INITIALIZER("("), 1, 5, C_TEST_AST_TYPE_PROBE_ANSWER,
     C_TYPE_UNSIGNED_CHAR},
    {S8_INITIALIZER("char* f(void* p) { return (char*)p; }"), S8_INITIALIZER("("), 1, 5, C_TEST_AST_TYPE_PROBE_ANSWER, C_TYPE_POINTER},
    {S8_INITIALIZER("char const* f(void* p) { return (const char*)p; }"), S8_INITIALIZER("("), 1, 6, C_TEST_AST_TYPE_PROBE_ANSWER, C_TYPE_POINTER},
    {S8_INITIALIZER("typedef int T; T* f(void* p) { return (T*)p; }"), S8_INITIALIZER("("), 1, 5, C_TEST_AST_TYPE_PROBE_ANSWER, C_TYPE_POINTER},
    {S8_INITIALIZER("typedef int T; T** f(void* p) { return (T**)p; }"), S8_INITIALIZER("("), 1, 6, C_TEST_AST_TYPE_PROBE_ANSWER, C_TYPE_POINTER},
    {S8_INITIALIZER("long f(int a) { return a + (long)a; }"), S8_INITIALIZER("a"), 1, 6, C_TEST_AST_TYPE_PROBE_ANSWER, C_TYPE_LONG},
    {S8_INITIALIZER("int f(void) { return (int){7}; }"), S8_INITIALIZER("("), 1, 6, C_TEST_AST_TYPE_PROBE_ANSWER, C_TYPE_INT},
    {S8_INITIALIZER("typedef long L; L f(int a) { return (L)&a; }"), S8_INITIALIZER("("), 1, 5, C_TEST_AST_TYPE_PROBE_ANSWER, C_TYPE_LONG},
    {S8_INITIALIZER("int f(int* p, int a) { return &p ? a : 0; }"), S8_INITIALIZER("&"), 0, 6, C_TEST_AST_TYPE_PROBE_ANSWER, C_TYPE_INT},
    // A cast of one string literal: with constraint checks the machine types
    // the literal too, appending its array row, so the answer replays that.
    {S8_INITIALIZER("typedef char C; C* f(void) { return (C*)\"text\"; }"), S8_INITIALIZER("("), 1, 5, C_TEST_AST_TYPE_PROBE_ANSWER,
     C_TYPE_POINTER, false, C_TYPE_INVALID, true},
    {S8_INITIALIZER("char const* f(void) { return (const char*)\"text\"; }"), S8_INITIALIZER("("), 1, 6, C_TEST_AST_TYPE_PROBE_ANSWER,
     C_TYPE_POINTER, false, C_TYPE_INVALID, true},
    // The `S8()` shape: the machine's operand task strips the parentheses.
    {S8_INITIALIZER("char* f(void) { return (char*)(\"a\"); }"), S8_INITIALIZER("("), 1, 7, C_TEST_AST_TYPE_PROBE_ANSWER, C_TYPE_POINTER, false,
     C_TYPE_INVALID, true},
    {S8_INITIALIZER("char* f(void) { return (char*)((\"a\")); }"), S8_INITIALIZER("("), 1, 9, C_TEST_AST_TYPE_PROBE_ANSWER, C_TYPE_POINTER, false,
     C_TYPE_INVALID, true},
    // Operands the machine types to nothing: `__func__` and its GNU spellings
    // name no entity, and an unbound `__builtin_expect` call reaches no typed
    // leaf. A checked cast skips its conversion rule over them, and a
    // conditional its scalar-condition rule, so both are answered with
    // constraint checks too. A void builtin called with no arguments is the
    // void row. The last case is BUSTER_CHECK's shape.
    {S8_INITIALIZER("char* f(void) { return (char*)__func__; }"), S8_INITIALIZER("("), 1, 5, C_TEST_AST_TYPE_PROBE_ANSWER, C_TYPE_POINTER},
    {S8_INITIALIZER("char* f(void) { return (char*)(__PRETTY_FUNCTION__); }"), S8_INITIALIZER("("), 1, 7, C_TEST_AST_TYPE_PROBE_ANSWER,
     C_TYPE_POINTER},
    {S8_INITIALIZER("long f(void) { return (long)__FUNCTION__; }"), S8_INITIALIZER("("), 1, 4, C_TEST_AST_TYPE_PROBE_ANSWER, C_TYPE_LONG},
    {S8_INITIALIZER("int f(int x) { return (int)__builtin_expect(x, 0); }"), S8_INITIALIZER("("), 1, 9, C_TEST_AST_TYPE_PROBE_ANSWER, C_TYPE_INT},
    {S8_INITIALIZER("int f(int x) { return __builtin_expect(x, 0) ? 1 : 2; }"), S8_INITIALIZER("__builtin_expect"), 0, 10,
     C_TEST_AST_TYPE_PROBE_ANSWER, C_TYPE_INT},
    {S8_INITIALIZER("int f(void) { return (__builtin_unreachable(), 0); }"), S8_INITIALIZER("__builtin_unreachable"), 0, 3,
     C_TEST_AST_TYPE_PROBE_ANSWER, C_TYPE_VOID},
    {S8_INITIALIZER("int f(void) { return (__builtin_debugtrap(), 0); }"), S8_INITIALIZER("("), 1, 7, C_TEST_AST_TYPE_PROBE_ANSWER, C_TYPE_INT},
    {S8_INITIALIZER("void f(int x) { (void)(__builtin_expect(!x, 0) ? (__builtin_debugtrap(), 0) : 0); }"), S8_INITIALIZER("("), 1, 22,
     C_TEST_AST_TYPE_PROBE_ANSWER, C_TYPE_VOID},
    // Answered without constraint checks only. With them the machine also
    // types a cast's operand -- here several string tokens, which no replay
    // covers -- and it decides whether two pointers may be subtracted by
    // comparing their element types, where the tree vouches only for one
    // shared row (each declared `char*` has its own `char` row; declarations
    // are never interned).
    {S8_INITIALIZER("long f(char* p, char* q) { return p - q; }"), S8_INITIALIZER("p"), 1, 3, C_TEST_AST_TYPE_PROBE_ANSWER, C_TYPE_LONG, true,
     C_TYPE_LONG_LONG},
    {S8_INITIALIZER("char* f(void) { return (char*)\"a\" \"b\"; }"), S8_INITIALIZER("("), 1, 6, C_TEST_AST_TYPE_PROBE_ANSWER, C_TYPE_POINTER,
     true},
    // One string-literal token alone, bare or parenthesized (`S8()`'s
    // `BUSTER_ARRAY_LENGTH` subscripts `(("text"))[0]`): the answer is the
    // array row the replay of the machine's leaf appends.
    {S8_INITIALIZER("char const* f(void) { return \"text\"; }"), S8_INITIALIZER("\"text\""), 0, 1, C_TEST_AST_TYPE_PROBE_ANSWER, C_TYPE_INVALID,
     false, C_TYPE_INVALID, false, true},
    {S8_INITIALIZER("char const* f(void) { return ((\"text\")); }"), S8_INITIALIZER("("), 1, 5, C_TEST_AST_TYPE_PROBE_ANSWER, C_TYPE_INVALID,
     false, C_TYPE_INVALID, false, true},
    {S8_INITIALIZER("unsigned short const* f(void) { return u\"text\"; }"), S8_INITIALIZER("u\"text\""), 0, 1, C_TEST_AST_TYPE_PROBE_ANSWER,
     C_TYPE_INVALID, false, C_TYPE_INVALID, false, true},
    // A member of a const or volatile struct or union: the machine's answer is
    // the member's qualified copy, which the analysis's own queries shared
    // before the probe (c_parse_intern_qualified_type), so the tree accepts
    // the node and answers with it: through `->` and `.`, in parentheses,
    // nested, for a member of aggregate or array type, and under a parent.
    {S8_INITIALIZER("struct S { int a; }; int f(struct S const* p) { return p->a; }"), S8_INITIALIZER("p"), 1, 3, C_TEST_AST_TYPE_PROBE_ANSWER,
     C_TYPE_INT},
    {S8_INITIALIZER("struct S { long a; }; long f(struct S volatile* p) { return p->a; }"), S8_INITIALIZER("p"), 1, 3, C_TEST_AST_TYPE_PROBE_ANSWER,
     C_TYPE_LONG},
    {S8_INITIALIZER("union U { long a; }; long f(union U volatile* p) { return (p->a); }"), S8_INITIALIZER("("), 1, 5,
     C_TEST_AST_TYPE_PROBE_ANSWER, C_TYPE_LONG},
    {S8_INITIALIZER("struct S { char* name; }; char* f(struct S const s) { return s.name; }"), S8_INITIALIZER("s"), 1, 3,
     C_TEST_AST_TYPE_PROBE_ANSWER, C_TYPE_POINTER},
    {S8_INITIALIZER("struct S { char* name; }; char* f(void) { const struct S s = { 0 }; return s.name; }"), S8_INITIALIZER("s"), 1, 3,
     C_TEST_AST_TYPE_PROBE_ANSWER, C_TYPE_POINTER},
    {S8_INITIALIZER("struct T { int x; }; struct S { struct T t; }; int f(struct S const* p) { return p->t.x; }"), S8_INITIALIZER("p"), 1, 3,
     C_TEST_AST_TYPE_PROBE_ANSWER, C_TYPE_STRUCT},
    {S8_INITIALIZER("struct T { int a; }; struct S { struct T t; }; int f(struct S const* p) { return p->t.a; }"), S8_INITIALIZER("p"), 1, 5,
     C_TEST_AST_TYPE_PROBE_ANSWER, C_TYPE_INT},
    {S8_INITIALIZER("struct S { int v[4]; }; int f(struct S const* p) { return p->v[1]; }"), S8_INITIALIZER("p"), 1, 3, C_TEST_AST_TYPE_PROBE_ANSWER,
     C_TYPE_ARRAY},
    {S8_INITIALIZER("struct S { int a; }; int f(struct S const* p) { return p->a + 1; }"), S8_INITIALIZER("p"), 1, 5,
     C_TEST_AST_TYPE_PROBE_ANSWER, C_TYPE_INT},
    {S8_INITIALIZER("struct S { int a; }; long f(struct S const* p) { return (long)p->a; }"), S8_INITIALIZER("("), 1, 6,
     C_TEST_AST_TYPE_PROBE_ANSWER, C_TYPE_LONG},
    // A copy that may not be shared, here because it would inherit an
    // alignment record: the node alone is answered by replaying the machine's
    // step (c_ast_types_decline), which appends the copy, but the node is not
    // accepted, so its parent declines.
    {S8_INITIALIZER("typedef int A16 __attribute__((aligned(16))); struct S { A16 a; }; int f(struct S const* p) { return p->a; }"),
     S8_INITIALIZER("p"), 1, 3, C_TEST_AST_TYPE_PROBE_ANSWER, C_TYPE_INVALID, false, C_TYPE_INVALID, false, true},
    {S8_INITIALIZER("typedef int A16 __attribute__((aligned(16))); struct S { A16 a; }; int f(struct S const* p) { return p->a + 1; }"),
     S8_INITIALIZER("p"), 1, 5, C_TEST_AST_TYPE_PROBE_DECLINE, C_TYPE_INVALID},
    // Declined: whatever would make the machine append a type row (a
    // qualified array element, a string, a cast to a type name with a
    // qualified typedef, a tag, a qualified or restrict pointer or a
    // declarator other than `*`, an array operand of `+`, a pointer
    // conditional), a builtin call, a parenthesized callee, and a conditional
    // whose middle operand holds an assignment, which the machine splits
    // there instead. A string literal of several tokens is left to the
    // machine.
    {S8_INITIALIZER("struct S { int v[4]; }; int f(struct S const* p) { return p->v[1]; }"), S8_INITIALIZER("p"), 1, 6, C_TEST_AST_TYPE_PROBE_DECLINE,
     C_TYPE_INVALID},
    {S8_INITIALIZER("char const* f(void) { return \"te\" \"xt\"; }"), S8_INITIALIZER("\"te\""), 0, 2, C_TEST_AST_TYPE_PROBE_DECLINE, C_TYPE_INVALID},
    {S8_INITIALIZER("typedef int T; T const* f(void* p) { return (const T*)p; }"), S8_INITIALIZER("("), 1, 6, C_TEST_AST_TYPE_PROBE_DECLINE,
     C_TYPE_INVALID},
    {S8_INITIALIZER("struct S; struct S* f(void* p) { return (struct S*)p; }"), S8_INITIALIZER("("), 1, 6, C_TEST_AST_TYPE_PROBE_DECLINE,
     C_TYPE_INVALID},
    {S8_INITIALIZER("char* f(void* p) { return (char* const)p; }"), S8_INITIALIZER("("), 1, 6, C_TEST_AST_TYPE_PROBE_DECLINE, C_TYPE_INVALID},
    {S8_INITIALIZER("char* f(void* p) { return (char* restrict)p; }"), S8_INITIALIZER("("), 1, 6, C_TEST_AST_TYPE_PROBE_DECLINE, C_TYPE_INVALID},
    {S8_INITIALIZER("int (*f(void* p))[4] { return (int(*)[4])p; }"), S8_INITIALIZER("("), 2, 10, C_TEST_AST_TYPE_PROBE_DECLINE, C_TYPE_INVALID},
    {S8_INITIALIZER("int b[4]; int* f(void) { return b + 1; }"), S8_INITIALIZER("b"), 1, 3, C_TEST_AST_TYPE_PROBE_DECLINE, C_TYPE_INVALID},
    {S8_INITIALIZER("int* f(int c, int* p, int* q) { return c ? p : q; }"), S8_INITIALIZER("c"), 1, 5, C_TEST_AST_TYPE_PROBE_DECLINE, C_TYPE_INVALID},
    {S8_INITIALIZER("int f(int c, int m) { return c ? m = 1 : 2; }"), S8_INITIALIZER("c"), 1, 7, C_TEST_AST_TYPE_PROBE_DECLINE, C_TYPE_INVALID},
    {S8_INITIALIZER("long f(void) { return __builtin_expect(1, 1); }"), S8_INITIALIZER("__builtin_expect"), 0, 6, C_TEST_AST_TYPE_PROBE_DECLINE,
     C_TYPE_INVALID},
    {S8_INITIALIZER("int g(int); int f(void) { return (g)(1); }"), S8_INITIALIZER("("), 2, 6, C_TEST_AST_TYPE_PROBE_DECLINE, C_TYPE_INVALID},
    {S8_INITIALIZER("void f(int* p) { __builtin_prefetch(p); }"), S8_INITIALIZER("__builtin_prefetch"), 0, 4, C_TEST_AST_TYPE_PROBE_DECLINE,
     C_TYPE_INVALID},
    // A designator is not member access, and a declaration is not an
    // expression.
    {S8_INITIALIZER("struct S { int field; }; void f(void) { struct S s = {.field = 1}; }"), S8_INITIALIZER("."), 0, 2, C_TEST_AST_TYPE_PROBE_MISS,
     C_TYPE_INVALID},
    {S8_INITIALIZER("void f(void) { int local = 1; }"), S8_INITIALIZER("int"), 0, 2, C_TEST_AST_TYPE_PROBE_MISS, C_TYPE_INVALID},
    // A file-scope object's initializer, typed for the static-initializer
    // walk. Names the binder recorded no use for resolve by spelling in the
    // file scope, as the machine resolves them.
    {S8_INITIALIZER("enum E { A, B }; int f = B;"), S8_INITIALIZER("B"), 1, 1, C_TEST_AST_TYPE_PROBE_ANSWER, C_TYPE_INT},
    {S8_INITIALIZER("enum E { A = 1 }; long f = A + 2L;"), S8_INITIALIZER("A"), 1, 3, C_TEST_AST_TYPE_PROBE_ANSWER, C_TYPE_LONG},
    {S8_INITIALIZER("long f = (7L);"), S8_INITIALIZER("("), 0, 3, C_TEST_AST_TYPE_PROBE_ANSWER, C_TYPE_LONG},
    {S8_INITIALIZER("unsigned long f = sizeof(\"ab\") / sizeof((\"ab\")[0]) - 1;"), S8_INITIALIZER("sizeof"), 0, 16, C_TEST_AST_TYPE_PROBE_ANSWER,
     C_TYPE_UNSIGNED_LONG, false, C_TYPE_UNSIGNED_LONG_LONG},
    {S8_INITIALIZER("typedef unsigned short U16; enum E { A }; U16 f = (U16)A;"), S8_INITIALIZER("("), 0, 4, C_TEST_AST_TYPE_PROBE_ANSWER,
     C_TYPE_UNSIGNED_SHORT},
    {S8_INITIALIZER("typedef struct P { int x; } P; P f = (P){1};"), S8_INITIALIZER("("), 0, 6, C_TEST_AST_TYPE_PROBE_ANSWER, C_TYPE_STRUCT},
    {S8_INITIALIZER("struct S { int a; int b; } f = { .a = 1, .b = sizeof(int) };"), S8_INITIALIZER("sizeof"), 0, 4, C_TEST_AST_TYPE_PROBE_ANSWER,
     C_TYPE_UNSIGNED_LONG, false, C_TYPE_UNSIGNED_LONG_LONG},
    // The static-initializer queries intern their primitive and pointer rows
    // as the body queries do, so `&` and casts to primitive words or a typedef
    // name under plain `*`s answer there too, the `S8()` cast with its replay.
    {S8_INITIALIZER("int g; int* f = &g;"), S8_INITIALIZER("&"), 0, 2, C_TEST_AST_TYPE_PROBE_ANSWER, C_TYPE_POINTER},
    {S8_INITIALIZER("long f = (long)7;"), S8_INITIALIZER("("), 0, 4, C_TEST_AST_TYPE_PROBE_ANSWER, C_TYPE_LONG},
    {S8_INITIALIZER("typedef char C; C** f = (C**)0;"), S8_INITIALIZER("("), 0, 6, C_TEST_AST_TYPE_PROBE_ANSWER, C_TYPE_POINTER},
    {S8_INITIALIZER("char* f = (char*)(\"a\");"), S8_INITIALIZER("("), 0, 7, C_TEST_AST_TYPE_PROBE_ANSWER, C_TYPE_POINTER, false,
     C_TYPE_INVALID, true},
    // A lone string literal is replayed. Declined or missed there as in a
    // body: a qualified pointer appends a row, and a designator is no
    // expression.
    {S8_INITIALIZER("char const* f = \"text\";"), S8_INITIALIZER("\"text\""), 0, 1, C_TEST_AST_TYPE_PROBE_ANSWER, C_TYPE_INVALID, false,
     C_TYPE_INVALID, false, true},
    {S8_INITIALIZER("char* f = (char* const)0;"), S8_INITIALIZER("("), 0, 6, C_TEST_AST_TYPE_PROBE_DECLINE, C_TYPE_INVALID},
    {S8_INITIALIZER("struct S { int a; } f = { .a = 1 };"), S8_INITIALIZER("."), 0, 2, C_TEST_AST_TYPE_PROBE_MISS, C_TYPE_INVALID},
};

BUSTER_GLOBAL_LOCAL u32 c_ast_test_token_index(CPreprocessResult preprocess, String8 spelling, u32 occurrence)
{
    u32 found = UINT32_MAX;
    u32 seen = 0;
    for (u32 index = 0; index < preprocess.token_count && found == UINT32_MAX; index += 1)
    {
        bool match = preprocess.tokens[index].kind != C_TOKEN_END_OF_FILE &&
                     string_equal(c_token_spelling(preprocess.spelling_base, preprocess.tokens[index]), spelling);
        found = match && seen == occurrence ? index : UINT32_MAX;
        seen += match;
    }
    return found;
}

BUSTER_GLOBAL_LOCAL UnitTestResult c_ast_test_types(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(c_ast_type_cases); index += 1)
    {
        CAstTypeCase const* type_case = c_ast_type_cases + index;
        TemporalArena temporary = scratch_begin(&arguments->arena, 1);
        CPreprocessResult preprocess = c_ast_test_preprocess(temporary.arena, type_case->source, C_PREPROCESS_DIALECT_GNU17);
        CAstResult built = c_ast_build(temporary.arena, preprocess, (CAstOptions){0});
        // The rows the probes' casts and `&` read are interned, as under the
        // pilot (CParserResult.type_interning).
        CParserResult syntax = c_parse_ast_reference(temporary.arena, preprocess);
        syntax.type_interning = true;
        CAnalysisResult analysis = c_analyze_semantics_only(temporary.arena, preprocess, syntax);
        u32 start = c_ast_test_token_index(preprocess, type_case->first, type_case->occurrence);
        if (BUSTER_REQUIRE(arguments, built.complete && !analysis.diagnostic_count && analysis.analysis_complete && start != UINT32_MAX))
        {
            for (u32 checked = 0; checked < 2; checked += 1)
            {
                CTestAstTypeProbe probe = c_test_ast_type_probe(temporary.arena, preprocess, &analysis, &built.ast, S8("f"), start,
                                                                start + type_case->count, checked != 0);
                bool declined = checked && type_case->unchecked_only;
                bool llp64 = target_uses_llp64_data_model(preprocess.target) && type_case->llp64_kind != C_TYPE_INVALID;
                u32 status = declined ? C_TEST_AST_TYPE_PROBE_DECLINE : type_case->status;
                CTypeKind kind = declined ? C_TYPE_INVALID : llp64 ? type_case->llp64_kind : type_case->kind;
                bool replay = (checked && type_case->replayed) || type_case->replay_answer;
                BUSTER_TEST_RAW(arguments, probe.status == status && probe.kind == kind && probe.nodes_typed > 0 && probe.replay == replay &&
                                               probe.replay_answer == type_case->replay_answer,
                                string_format(temporary.arena,
                                              S8("{S8} (checked {u32}): status {u32} kind {u32} replay {u32}, expected status {u32} kind {u32} replay {u32}"),
                                              type_case->source, checked, probe.status, (u32)probe.kind, (u32)probe.replay, status, (u32)kind,
                                              (u32)replay));
                BUSTER_TEST(arguments, !probe.nonplace_projection);
            }
        }
        c_ast_release(&built.ast);
        c_test_scratch_end(temporary);
    }
    // A function without a body, and a source without that function.
    TemporalArena temporary = scratch_begin(&arguments->arena, 1);
    CPreprocessResult preprocess = c_ast_test_preprocess(temporary.arena, S8("int f(void); int g(void) { return f(); }"), C_PREPROCESS_DIALECT_GNU17);
    CAstResult built = c_ast_build(temporary.arena, preprocess, (CAstOptions){0});
    CAnalysisResult analysis = c_analyze_semantics_only(temporary.arena, preprocess, c_parse_ast_reference(temporary.arena, preprocess));
    if (BUSTER_REQUIRE(arguments, built.complete && !analysis.diagnostic_count))
    {
        u32 start = c_ast_test_token_index(preprocess, S8("f"), 1);
        BUSTER_TEST(arguments, c_test_ast_type_probe(temporary.arena, preprocess, &analysis, &built.ast, S8("f"), start, start + 3, false).status ==
                                   C_TEST_AST_TYPE_PROBE_NO_BODY);
        CTestAstTypeProbe probe = c_test_ast_type_probe(temporary.arena, preprocess, &analysis, &built.ast, S8("g"), start, start + 3, false);
        BUSTER_TEST(arguments, probe.status == C_TEST_AST_TYPE_PROBE_ANSWER && probe.kind == C_TYPE_INT);
    }
    c_ast_release(&built.ast);
    c_test_scratch_end(temporary);
    // Without interned rows, the default path, the typer finds no `int *` row
    // for `&` to answer with, and declines it. The tree's column reservations
    // are back where they started once it is released.
    temporary = scratch_begin(&arguments->arena, 1);
    preprocess = c_ast_test_preprocess(temporary.arena, S8("int* f(int a) { return &a; }"), C_PREPROCESS_DIALECT_GNU17);
    analysis = c_analyze_semantics_only(temporary.arena, preprocess, c_parse_ast_reference(temporary.arena, preprocess));
    u64 reserved = arena_test_live_reserved_bytes();
    built = c_ast_build(temporary.arena, preprocess, (CAstOptions){0});
    u32 address = c_ast_test_token_index(preprocess, S8("&"), 0);
    if (BUSTER_REQUIRE(arguments, built.complete && !analysis.diagnostic_count && address != UINT32_MAX))
    {
        for (u32 checked = 0; checked < 2; checked += 1)
        {
            CTestAstTypeProbe probe = c_test_ast_type_probe(temporary.arena, preprocess, &analysis, &built.ast, S8("f"), address, address + 2,
                                                            checked != 0);
            BUSTER_TEST(arguments, probe.status == C_TEST_AST_TYPE_PROBE_DECLINE);
        }
    }
    c_ast_release(&built.ast);
    BUSTER_TEST(arguments, arena_test_live_reserved_bytes() == reserved);
    c_test_scratch_end(temporary);
    // Array parameter adjustment also declines without the shared rows;
    // the ordinary token query still accepts this valid pointer return.
    temporary = scratch_begin(&arguments->arena, 1);
    preprocess = c_ast_test_preprocess(temporary.arena, S8("char*** f(char* a[]) { return &a; }"), C_PREPROCESS_DIALECT_GNU17);
    analysis = c_analyze_semantics_only(temporary.arena, preprocess, c_parse_ast_reference(temporary.arena, preprocess));
    built = c_ast_build(temporary.arena, preprocess, (CAstOptions){0});
    address = c_ast_test_token_index(preprocess, S8("&"), 0);
    if (BUSTER_REQUIRE(arguments, built.complete && !analysis.diagnostic_count && address != UINT32_MAX))
    {
        for (u32 checked = 0; checked < 2; checked += 1)
        {
            CTestAstTypeProbe probe = c_test_ast_type_probe(temporary.arena, preprocess, &analysis, &built.ast, S8("f"), address, address + 2,
                                                            checked != 0);
            BUSTER_TEST(arguments, probe.status == C_TEST_AST_TYPE_PROBE_DECLINE);
        }
    }
    c_ast_release(&built.ast);
    c_test_scratch_end(temporary);
    // A late row: when no query has shared the member's qualified copy yet,
    // here because the analysis ran without interned rows, the machine would
    // append the copy, so the typer does not accept the member of a const
    // struct. The node alone is answered by the replay; its parent declines.
    temporary = scratch_begin(&arguments->arena, 1);
    preprocess = c_ast_test_preprocess(temporary.arena, S8("struct S { int a; }; int f(struct S const* p) { return p->a + 1; }"),
                                       C_PREPROCESS_DIALECT_GNU17);
    analysis = c_analyze_semantics_only(temporary.arena, preprocess, c_parse_ast_reference(temporary.arena, preprocess));
    built = c_ast_build(temporary.arena, preprocess, (CAstOptions){0});
    u32 member = c_ast_test_token_index(preprocess, S8("p"), 1);
    if (BUSTER_REQUIRE(arguments, built.complete && !analysis.diagnostic_count && member != UINT32_MAX))
    {
        for (u32 checked = 0; checked < 2; checked += 1)
        {
            CTestAstTypeProbe probe = c_test_ast_type_probe(temporary.arena, preprocess, &analysis, &built.ast, S8("f"), member, member + 3,
                                                            checked != 0);
            BUSTER_TEST(arguments, probe.status == C_TEST_AST_TYPE_PROBE_ANSWER && probe.replay_answer);
            probe = c_test_ast_type_probe(temporary.arena, preprocess, &analysis, &built.ast, S8("f"), member, member + 5, checked != 0);
            BUSTER_TEST(arguments, probe.status == C_TEST_AST_TYPE_PROBE_DECLINE);
        }
    }
    c_ast_release(&built.ast);
    c_test_scratch_end(temporary);
    // The const-assignment walk does not ask about the designator probes of
    // `{ .f = 1, .g = 2 }`: the `{` and the `,` it would take for the bases of
    // `.f` and `.g`, and the `.f` and `.g` places before each `=`. Verify mode
    // has the machine answer all four, and each must fail without a
    // diagnostic or a new table row.
    temporary = scratch_begin(&arguments->arena, 1);
    preprocess = c_ast_test_preprocess(temporary.arena, S8("struct S { int f; int g; }; void h(void) { struct S s = { .f = 1, .g = 2 }; }"),
                                       C_PREPROCESS_DIALECT_GNU17);
    c_test_ast_type_verify_take();
    c_test_ast_type_verify_set(true);
    analysis = c_analyze_semantics_only(temporary.arena, preprocess, c_parse_ast_reference(temporary.arena, preprocess));
    c_test_ast_type_verify_set(false);
    CTestAstTypeVerify verify = c_test_ast_type_verify_take();
    BUSTER_TEST(arguments, analysis.analysis_complete && !analysis.diagnostic_count && verify.probes == 4 && verify.probe_mismatches == 0);
    c_test_scratch_end(temporary);
    // A member of a const aggregate whose type is an aligned typedef: the
    // replay's qualified copy inherits the typedef's alignment record, and
    // verify mode holds that record to the machine's, field by field. With
    // the held record's range moved, the comparison must fail.
    for (u32 perturb = 0; perturb < 2; perturb += 1)
    {
        temporary = scratch_begin(&arguments->arena, 1);
        preprocess = c_ast_test_preprocess(temporary.arena,
                                           S8("typedef int A __attribute__((aligned(16))); struct S { A a; }; A f(struct S const* p) { return p->a; }"),
                                           C_PREPROCESS_DIALECT_GNU17);
        built = c_ast_build(temporary.arena, preprocess, (CAstOptions){0});
        if (BUSTER_REQUIRE(arguments, built.complete))
        {
            CParserResult syntax = c_parse_ast_reference(temporary.arena, preprocess);
            syntax.ast = &built.ast;
            syntax.type_interning = true;
            c_test_ast_type_verify_take();
            c_test_ast_type_verify_set(true);
            c_test_ast_type_verify_perturb_alignments(perturb != 0);
            analysis = c_analyze_semantics_only(temporary.arena, preprocess, syntax);
            c_test_ast_type_verify_perturb_alignments(false);
            c_test_ast_type_verify_set(false);
            verify = c_test_ast_type_verify_take();
            BUSTER_TEST(arguments, analysis.analysis_complete && !analysis.diagnostic_count && verify.compared > 0);
            BUSTER_TEST(arguments, perturb ? verify.mismatches > 0 && verify.first[0].reasons == C_TEST_AST_TYPE_MISMATCH_TABLES : verify.mismatches == 0);
        }
        c_ast_release(&built.ast);
        c_test_scratch_end(temporary);
    }
    return result;
}

// The typer's name checks (c_ast_types_lookups_settled): a region whose
// marked names no entity outside the file scope shares settles once and
// answers its checks without a lookup; a name that a parameter or a local
// also spells is checked by walking the query's subtree, and so is every
// check of a region with more such names than it records. A name's
// file-scope answer is looked up once and reused by later regions. Each
// source is analyzed with the tree, and again with every tree answer
// verified against the machine.
typedef struct CAstTypeLookupCase CAstTypeLookupCase;
struct CAstTypeLookupCase
{
    String8 source;
    bool settled;
    bool walked;
    // Some query is declined: its name resolves to another entity in the
    // query's scope.
    bool declined;
};

BUSTER_GLOBAL_LOCAL CAstTypeLookupCase const c_ast_type_lookup_cases[] = {
    // File-scope callees, a typedef name and an enumerator in a body.
    {S8_INITIALIZER("typedef long L; enum { A = 3 }; int g(int); long f(int a) { int r = g(a); { long s = (L)g(r); r += (int)s; } return (L)r + A; }"),
     true, false, false},
    // A callee through a parameter is checked by the walk; the file-scope
    // callee in the same body is still settled.
    {S8_INITIALIZER("int g(int); int f(int (*h)(int), int a) { int r = h(a); return r + g(a); }"), true, true, false},
    // A local declared after the call shadows the callee in the query's
    // scope, so the walk declines the call.
    {S8_INITIALIZER("int g(void); int f(void) { int r = g(); int g = 2; return r + g; }"), false, true, true},
    // The same name as a local of another function only.
    {S8_INITIALIZER("int g(void); int h(void) { int g = 1; return g; } int f(void) { int r = g(); return r + g(); }"), false, true, false},
    // More callees through parameters than a region records.
    {S8_INITIALIZER("int g(int); int f(int (*h)(int)) { return g(0) + h(1) + h(2) + h(3) + h(4) + h(5) + h(6) + h(7) + h(8) + h(9) + h(10) + h(11) + "
                    "h(12) + h(13) + h(14) + h(15) + h(16) + h(17); }"),
     false, true, false},
    // One callee in three regions: the regions after the first settle on the
    // file scope's answer that the first looked up.
    {S8_INITIALIZER("int g(int); int f(int a) { return g(a) + g(2); } int h(int a) { return g(a) + g(1); } int k(int a) { int r = g(a); return r; }"),
     true, false, false},
    // A callee declared, then defined between its uses.
    {S8_INITIALIZER("int g(int); int f(int a) { return g(a); } int g(int a) { return a; } int h(int a) { return g(a) + g(1); }"), true, false, false},
    // File-scope initializers: unbound names and typedef names resolve in the
    // file scope; one a local elsewhere also spells is walked.
    {S8_INITIALIZER("typedef unsigned short U16; enum { A = 3 }; long x = (U16)A + 1;"), true, false, false},
    {S8_INITIALIZER("enum { v = 4 }; int f(void) { int v = 1; return v; } long x = v + 1L;"), false, true, false},
};

BUSTER_GLOBAL_LOCAL UnitTestResult c_ast_test_type_lookups(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(c_ast_type_lookup_cases); index += 1)
    {
        CAstTypeLookupCase const* lookup_case = c_ast_type_lookup_cases + index;
        TemporalArena temporary = scratch_begin(&arguments->arena, 1);
        CPreprocessResult preprocess = c_ast_test_preprocess(temporary.arena, lookup_case->source, C_PREPROCESS_DIALECT_GNU17);
        CAstResult built = c_ast_build(temporary.arena, preprocess, (CAstOptions){0});
        if (BUSTER_REQUIRE(arguments, built.complete))
        {
            CAstTypeStatistics statistics = {0};
            CParserResult typed_syntax = c_parse_ast_reference(temporary.arena, preprocess);
            typed_syntax.ast = &built.ast;
            typed_syntax.ast_type_statistics = &statistics;
            typed_syntax.type_interning = true;
            CAnalysisResult typed = c_analyze_semantics_only(temporary.arena, preprocess, typed_syntax);
            BUSTER_TEST_RAW(arguments,
                            typed.analysis_complete && !typed.diagnostic_count && (statistics.lookups_settled != 0) == lookup_case->settled &&
                                (statistics.lookups_walked != 0) == lookup_case->walked && (!lookup_case->declined || statistics.declines),
                            string_format(temporary.arena, S8("{S8}: {u64} settled, {u64} walked, {u64} answers, {u64} declines, {u32} diagnostics"),
                                          lookup_case->source, statistics.lookups_settled, statistics.lookups_walked, statistics.answers,
                                          statistics.declines, typed.diagnostic_count));
            CParserResult verified_syntax = c_parse_ast_reference(temporary.arena, preprocess);
            verified_syntax.ast = &built.ast;
            verified_syntax.type_interning = true;
            c_test_ast_type_verify_take();
            c_test_ast_type_verify_set(true);
            c_analyze_semantics_only(temporary.arena, preprocess, verified_syntax);
            c_test_ast_type_verify_set(false);
            CTestAstTypeVerify verify = c_test_ast_type_verify_take();
            BUSTER_TEST_RAW(arguments, verify.compared && !verify.mismatches,
                            string_format(temporary.arena, S8("{S8}: {u64} of {u64} verified answers disagree with the machine"), lookup_case->source,
                                          verify.mismatches, verify.compared));
        }
        c_ast_release(&built.ast);
        c_test_scratch_end(temporary);
    }
    return result;
}

// c_parse_ast_from_tree on one shape per fallback reason, and on shapes it
// publishes itself, in every layout: the result is always c_parse_ast's, and
// the statistics name the reason. The #3215 and #3368 shapes, including a
// redundant group after a decorated typedef name, are among the published ones.
typedef struct CAstSplitCase CAstSplitCase;
struct CAstSplitCase
{
    String8 source;
    CPreprocessDialect dialect;
    CParserTreeFallback reason;
    // Body _Static_assert ranges the split publishes, when it publishes.
    u32 assertions;
    // Records the walker marks typedef and constexpr (#3310).
    u32 typedef_records;
    u32 constexpr_records;
};

BUSTER_GLOBAL_LOCAL CAstSplitCase const c_ast_split_cases[] = {
    {S8_INITIALIZER("int a, *b, f(int), (*g)(int, ...); struct S { int x; }; enum { A, B }; _Static_assert(1, \"m\"); asm(\"nop\"); ; "
                    "int h(int x) { _Static_assert(1, \"n\"); struct T { _Static_assert(2, \"o\"); int a; } t = {x}; return t.a; }"),
     C_PREPROCESS_DIALECT_GNU17, C_PARSER_TREE_FALLBACK_NONE, 2},
    // Only the top-level specifiers' `typedef` and `constexpr` words mark a
    // declaration (#3310): a statement expression's typedef and a constexpr
    // compound literal in an initializer leave `sx`, `sy`, `cy` and `cz`
    // ordinary objects, while `q9` and `cw` keep their own words.
    {S8_INITIALIZER("int sx = ({ typedef int T9; T9 t = 1; t; }), sy; struct Q9 { int a; } typedef q9;"), C_PREPROCESS_DIALECT_GNU17,
     C_PARSER_TREE_FALLBACK_NONE, 0, 1, 0},
    {S8_INITIALIZER("int cy = (constexpr int){3}, cz; constexpr int cw = 2;"), C_PREPROCESS_DIALECT_C23, C_PARSER_TREE_FALLBACK_NONE, 0, 0, 1},
    {S8_INITIALIZER("int x = 0x;"), C_PREPROCESS_DIALECT_GNU17, C_PARSER_TREE_FALLBACK_DIAGNOSTIC},
    {S8_INITIALIZER("long long long y;"), C_PREPROCESS_DIALECT_GNU17, C_PARSER_TREE_FALLBACK_DIAGNOSTIC},
    // The probe skips a specifier run spelled like one that already
    // validated clean, so a repeated run must not hide a later invalid one,
    // whether keyed or too long to key.
    {S8_INITIALIZER("long long a; long long b; long long long c;"), C_PREPROCESS_DIALECT_GNU17, C_PARSER_TREE_FALLBACK_DIAGNOSTIC},
    {S8_INITIALIZER("unsigned int a; unsigned int b; unsigned unsigned c;"), C_PREPROCESS_DIALECT_GNU17, C_PARSER_TREE_FALLBACK_DIAGNOSTIC},
    {S8_INITIALIZER("static const volatile unsigned long long int a; static const volatile long long long int b;"), C_PREPROCESS_DIALECT_GNU17,
     C_PARSER_TREE_FALLBACK_DIAGNOSTIC},
    {S8_INITIALIZER("int a; int b; static const int c; static const int d; struct S { int x; } s; struct S t;"), C_PREPROCESS_DIALECT_GNU17,
     C_PARSER_TREE_FALLBACK_NONE, 0},
    {S8_INITIALIZER("_Alignas(8) int a;"), C_PREPROCESS_DIALECT_GNU17, C_PARSER_TREE_FALLBACK_SPECIFIERS},
    {S8_INITIALIZER("__typeof__(1) t;"), C_PREPROCESS_DIALECT_GNU17, C_PARSER_TREE_FALLBACK_SPECIFIERS},
    // The walker read the `(int)` operand and `(av)` as the function shape
    // `(f)(...)` named `int` (#3368).
    {S8_INITIALIZER("_Atomic(int) (av); _Atomic(int) (fd(int a)) { return a; }"), C_PREPROCESS_DIALECT_GNU17, C_PARSER_TREE_FALLBACK_SPECIFIERS},
    {S8_INITIALIZER("enum E : int { A };"), C_PREPROCESS_DIALECT_C23, C_PARSER_TREE_FALLBACK_SPECIFIERS},
    // #3215: redundant groups, published since the walker walks out of them.
    {S8_INITIALIZER("int f(int x) { return x + 1; } int ((*pq))(int) = f;"), C_PREPROCESS_DIALECT_GNU17, C_PARSER_TREE_FALLBACK_NONE, 0},
    {S8_INITIALIZER("int (gd(int a)) { return a; } int zz;"), C_PREPROCESS_DIALECT_GNU17, C_PARSER_TREE_FALLBACK_NONE, 0},
    {S8_INITIALIZER("int (x); int ((y))[3]; void ((h))(void) {}"), C_PREPROCESS_DIALECT_GNU17, C_PARSER_TREE_FALLBACK_NONE, 0},
    // #3368: a single redundant group after a tag or typedef name, published
    // since the walker reads it as a declarator group.
    {S8_INITIALIZER("struct P { int a; }; struct P (pg(void)) { struct P r = {0}; return r; }"), C_PREPROCESS_DIALECT_GNU17,
     C_PARSER_TREE_FALLBACK_NONE, 0},
    {S8_INITIALIZER("typedef int T; T (x); static const T (y), (z)[2]; T (tg(int a)) { return a; } struct S { int a; } (s); struct S (s2);"),
     C_PREPROCESS_DIALECT_GNU17, C_PARSER_TREE_FALLBACK_NONE, 0, 1},
    // Decorations among the words before the typedef name leave it in the
    // type's position, so the group is still a declarator group.
    {S8_INITIALIZER("typedef int T; __attribute__((unused)) T (x); static __attribute__((unused)) const T (y), (z)[2]; "
                    "__attribute__((unused)) T (tg(int a)) { return a; } __extension__ T (w);"),
     C_PREPROCESS_DIALECT_GNU17, C_PARSER_TREE_FALLBACK_NONE, 0, 1},
    {S8_INITIALIZER("typedef int T; [[maybe_unused]] T (x); [[maybe_unused]] static T (y);"), C_PREPROCESS_DIALECT_C23, C_PARSER_TREE_FALLBACK_NONE, 0, 1},
    {S8_INITIALIZER("int * __attribute__((x)) p;"), C_PREPROCESS_DIALECT_GNU17, C_PARSER_TREE_FALLBACK_DECLARATOR},
    // The walker reads `...` only before the declarator's last `)`.
    {S8_INITIALIZER("int (gv(int a, ...)) { return a; }"), C_PREPROCESS_DIALECT_GNU17, C_PARSER_TREE_FALLBACK_TOKENS},
    {S8_INITIALIZER("int k(a) int a; { return a; }"), C_PREPROCESS_DIALECT_GNU17, C_PARSER_TREE_FALLBACK_OLD_STYLE},
    // #3368: a list inside or after a redundant group is the name's list
    // too, so an old-style declaration list after it is refused, and the
    // identifier list of a definition without one is published.
    {S8_INITIALIZER("int (k(a)) int a; { return a; }"), C_PREPROCESS_DIALECT_GNU17, C_PARSER_TREE_FALLBACK_OLD_STYLE},
    {S8_INITIALIZER("typedef int T; int (k(T)) { return 0; } int (*(m(T)))(void) { return 0; } int x, (kd(int c)), (k2)(T);"),
     C_PREPROCESS_DIALECT_GNU17, C_PARSER_TREE_FALLBACK_NONE, 0, 1, 0},
    {S8_INITIALIZER("int e = (1, 2);"), C_PREPROCESS_DIALECT_GNU17, C_PARSER_TREE_FALLBACK_TOKENS},
    {S8_INITIALIZER("int h8(void) [[deprecated]];"), C_PREPROCESS_DIALECT_C23, C_PARSER_TREE_FALLBACK_TOKENS},
    {S8_INITIALIZER("struct P { int a, b; }; _Static_assert(sizeof (struct P){1, 2} == 8, \"m\");"), C_PREPROCESS_DIALECT_GNU17,
     C_PARSER_TREE_FALLBACK_ASSERTION},
    {S8_INITIALIZER("void f(void) { _Static_assert(1); }"), C_PREPROCESS_DIALECT_C23, C_PARSER_TREE_FALLBACK_ASSERTION},
};

BUSTER_GLOBAL_LOCAL UnitTestResult c_ast_test_split(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(c_ast_split_cases); index += 1)
    {
        CAstSplitCase const* split_case = &c_ast_split_cases[index];
        for (u32 layout = 0; layout < BUSTER_ARRAY_LENGTH(c_ast_test_layouts); layout += 1)
        {
            TemporalArena temporary = scratch_begin(&arguments->arena, 1);
            String8 label = string_format(temporary.arena, S8("split case {u32}, layout {u32}"), index, layout);
            CPreprocessResult preprocess = c_ast_test_preprocess(temporary.arena, split_case->source, split_case->dialect);
            CAstResult built = c_ast_build(temporary.arena, preprocess, (CAstOptions){.layout = c_ast_test_layouts[layout]});
            if (BUSTER_REQUIRE(arguments, preprocess.error_count == 0 && built.complete))
            {
                CParserResult syntax = c_parse_ast_reference(temporary.arena, preprocess);
                CParserTreeStatistics statistics = {0};
                CParserResult derived = c_parse_ast_from_tree(temporary.arena, preprocess, &built.ast, &statistics);
                String8 difference = c_ast_corpus_split_differ(temporary.arena, &syntax, &derived);
                BUSTER_TEST_RAW(arguments, difference.length == 0, label);
                bool published = split_case->reason == C_PARSER_TREE_FALLBACK_NONE;
                BUSTER_TEST_RAW(arguments, statistics.units == (u64)published && statistics.fallbacks == (u64)!published, label);
                BUSTER_TEST_RAW(arguments, published || (statistics.reason == split_case->reason && statistics.fallback_counts[split_case->reason] == 1),
                                label);
                BUSTER_TEST_RAW(arguments, !published || (statistics.records == syntax.declaration_count && statistics.assertions == split_case->assertions),
                                label);
                u32 typedef_records = 0;
                u32 constexpr_records = 0;
                for (CParserDeclaration const* declaration = syntax.first_declaration; declaration; declaration = declaration->next)
                {
                    typedef_records += declaration->is_typedef;
                    constexpr_records += declaration->is_constexpr;
                }
                BUSTER_TEST_RAW(arguments, typedef_records == split_case->typedef_records && constexpr_records == split_case->constexpr_records, label);
            }
            c_ast_release(&built.ast);
            c_test_scratch_end(temporary);
        }
    }
    // Without a tree, and with an incomplete one, the walker answers.
    {
        TemporalArena temporary = scratch_begin(&arguments->arena, 1);
        CPreprocessResult preprocess = c_ast_test_preprocess(temporary.arena, S8("int a;"), C_PREPROCESS_DIALECT_GNU17);
        CParserTreeStatistics statistics = {0};
        CParserResult syntax = c_parse_ast_reference(temporary.arena, preprocess);
        CParserResult derived = c_parse_ast_from_tree(temporary.arena, preprocess, 0, &statistics);
        BUSTER_TEST(arguments, c_ast_corpus_split_differ(temporary.arena, &syntax, &derived).length == 0);
        CAst empty = {0};
        derived = c_parse_ast_from_tree(temporary.arena, preprocess, &empty, &statistics);
        BUSTER_TEST(arguments, c_ast_corpus_split_differ(temporary.arena, &syntax, &derived).length == 0);
        BUSTER_TEST(arguments, statistics.fallbacks == 2 && statistics.fallback_counts[C_PARSER_TREE_FALLBACK_INPUT] == 2 && statistics.units == 0);
        BUSTER_TEST(arguments, string_equal(c_parser_tree_fallback_name(C_PARSER_TREE_FALLBACK_DECLARATOR), S8("declarator")));
        c_test_scratch_end(temporary);
    }
    return result;
}

// The body binder's statement plan (c_parse.c, #3102) on chosen bodies, in
// every layout: one body that uses every kind of plan entry, one whose
// statements the plan leaves to the token walk, and one shape per
// CAstStatementFallback -- a tree whose function body the record does not
// have, and one whose expression statement is anchored off its `;`. Each
// analysis with the tree must bind exactly as the analysis without it.
typedef enum CAstStatementTamper
{
    C_AST_STATEMENT_TAMPER_NONE,
    C_AST_STATEMENT_TAMPER_BRACE,
    C_AST_STATEMENT_TAMPER_SEMICOLON,
    // The second declarator's name moved onto the first's, so its `=` hint
    // lands before its own tokens and the split refuses it.
    C_AST_STATEMENT_TAMPER_DECLARATOR,
    // The first initializer list's anchor moved off its `{`, so the plan
    // leaves that statement to the loop.
    C_AST_STATEMENT_TAMPER_LIST,
} CAstStatementTamper;

typedef struct CAstStatementCase CAstStatementCase;
struct CAstStatementCase
{
    String8 source;
    CAstStatementTamper tamper;
    CAstStatementFallback reason;
    // Floors on what a planned body takes from its plan.
    u32 segments;
    u32 declaration_hints;
    u32 loop_hints;
    u32 declines;
    // Segments handed back to the token walk at a token it reads itself
    // (here `__builtin_offsetof`).
    u32 bails;
    // Floors on the block declarations' declarator hints and plain
    // initializers, and the exact count of hints the split refuses.
    u32 declarator_hints;
    u32 initializer_hints;
    u32 declarator_declines;
    // The exact count of statements planned around initializer braces.
    u32 braced;
};

#define C_AST_STATEMENT_CASE_EVERY                                                                                                             \
    "int g(int);\n"                                                                                                                          \
    "int f(int n)\n"                                                                                                                         \
    "{\n"                                                                                                                                    \
    "    int a = 1, b[2] = {1, 2};\n"                                                                                                        \
    "    unsigned s = 0;\n"                                                                                                                  \
    "    for (int i = 0; i < n; i++) s += (unsigned)i;\n"                                                                                    \
    "    for (a = 0; a < n; a += 1) { b[0] = a; }\n"                                                                                         \
    "    for (;;) break;\n"                                                                                                                  \
    "    while (a > 0) a--;\n"                                                                                                               \
    "    do { a++; continue; } while ((a < 3));\n"                                                                                           \
    "    if (a) s = 1; else if (b[1]) s = 2; else { s = 3; }\n"                                                                              \
    "    switch (n) { case 1: s = 4; break; case 2: default: s = 5; }\n"                                                                     \
    "    goto done;\n"                                                                                                                       \
    "    ;\n"                                                                                                                                \
    "done:\n"                                                                                                                                \
    "    return (int)s + g(a) + (int)sizeof(unsigned long);\n"                                                                               \
    "}\n"

#define C_AST_STATEMENT_CASE_DECLARATORS                                                                                                       \
    "struct P { int x, y; };\n"                                                                                                               \
    "int k(int v)\n"                                                                                                                          \
    "{\n"                                                                                                                                     \
    "    int a = v, b[3] = {[1] = v, 2}, *c = &a;\n"                                                                                          \
    "    struct P p = {.x = a, .y = b[1]}, q = (struct P){v, a};\n"                                                                           \
    "    unsigned long o = __builtin_offsetof(struct P, y) + sizeof(struct P);\n"                                                             \
    "    int u __attribute__((unused)) = 3;\n"                                                                                                \
    "    __auto_type t = p.x;\n"                                                                                                              \
    "    int s = ({ int z = v; z; });\n"                                                                                                      \
    "    return a + *c + q.y + (int)o + u + t + s;\n"                                                                                         \
    "}\n"

// Compound literals in every statement position the plan splits at their
// braces: expression statements (with designators of each kind, a trailing
// comma, nested and empty lists, a postfix after a `}` and a string run), an
// if, while and switch header, a do-while tail and a return. One holds
// __builtin_offsetof, which the loop reads, as its list's first token; one
// holds more lists than the plan takes and is left to the loop.
#define C_AST_STATEMENT_CASE_BRACED                                                                                                            \
    "struct P { int x, y; };\n"                                                                                                               \
    "struct Q { struct P p[2]; int n; };\n"                                                                                                   \
    "int use(struct P);\n"                                                                                                                    \
    "int m(int v, int w)\n"                                                                                                                   \
    "{\n"                                                                                                                                     \
    "    struct P p;\n"                                                                                                                       \
    "    p = (struct P){.x = v, .y = w};\n"                                                                                                   \
    "    use((struct P){v, });\n"                                                                                                             \
    "    p.y = (struct Q){{[1] = {.y = v}}, .n = w,}.n;\n"                                                                                    \
    "    p.x = ((int[]){1, 2, [3 ... 4] = v})[w & 1] + (struct P){}.y;\n"                                                                     \
    "    p.x = (int){__builtin_offsetof(struct P, y)};\n"                                                                                     \
    "    p.y = (int[][1]){{1}, {2}, {3}, {4}, {5}, {6}, {7}, {8}, {9}, {10}, {11}, {12}, {13}, {14}, {15}, {16}}[w][0];\n"                    \
    "    if ((struct P){v, w}.x) p.x = 1;\n"                                                                                                  \
    "    while ((int){v} > w) w++;\n"                                                                                                         \
    "    switch ((int){w}) { default: break; }\n"                                                                                             \
    "    do { v--; } while ((struct P){v}.x > 0);\n"                                                                                          \
    "    return use((struct P){.x = p.x, .y = (int)sizeof(\"a\" \"b\")});\n"                                                                \
    "}\n"

BUSTER_GLOBAL_LOCAL CAstStatementCase const c_ast_statement_cases[] = {
    {S8_INITIALIZER(C_AST_STATEMENT_CASE_EVERY), C_AST_STATEMENT_TAMPER_NONE, C_AST_STATEMENT_FALLBACK_NONE, 10, 2, 3, 1, 0, 3, 3, 0},
    {S8_INITIALIZER(C_AST_STATEMENT_CASE_DECLARATORS), C_AST_STATEMENT_TAMPER_NONE, C_AST_STATEMENT_FALLBACK_NONE, 1, 5, 0, 1, 0, 8, 8, 0},
    {S8_INITIALIZER(C_AST_STATEMENT_CASE_DECLARATORS), C_AST_STATEMENT_TAMPER_DECLARATOR, C_AST_STATEMENT_FALLBACK_NONE, 1, 5, 0, 1, 0, 7, 7, 1},
    {S8_INITIALIZER("struct P { int x; };\n"
                    "int h(int v)\n"
                    "{\n"
                    "    struct Q { int q; } local = {v};\n"
                    "    int w = ({ int t = v; t + 1; });\n"
                    "    struct P p = (struct P){.x = w};\n"
                    "    __asm__ volatile(\"\" ::: \"memory\");\n"
                    "    __attribute__((unused)) int u;\n"
                    "    w += __builtin_offsetof(struct P, x);\n"
                    "    w = ({ int z = w; z; });\n"
                    "again:\n"
                    "    if (w < 0) goto again;\n"
                    "    return p.x + local.q;\n"
                    "}\n"),
     C_AST_STATEMENT_TAMPER_NONE, C_AST_STATEMENT_FALLBACK_NONE, 1, 1, 0, 4, 1, 2, 1, 0},
    {S8_INITIALIZER(C_AST_STATEMENT_CASE_BRACED), C_AST_STATEMENT_TAMPER_NONE, C_AST_STATEMENT_FALLBACK_NONE, 10, 1, 0, 1, 1, 0, 0, 0, 10},
    {S8_INITIALIZER(C_AST_STATEMENT_CASE_BRACED), C_AST_STATEMENT_TAMPER_LIST, C_AST_STATEMENT_FALLBACK_NONE, 10, 1, 0, 2, 1, 0, 0, 0, 9},
    {S8_INITIALIZER(C_AST_STATEMENT_CASE_EVERY), C_AST_STATEMENT_TAMPER_BRACE, C_AST_STATEMENT_FALLBACK_INPUT},
    {S8_INITIALIZER(C_AST_STATEMENT_CASE_EVERY), C_AST_STATEMENT_TAMPER_SEMICOLON, C_AST_STATEMENT_FALLBACK_TOKENS},
};

BUSTER_GLOBAL_LOCAL UnitTestResult c_ast_test_statements(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(c_ast_statement_cases); index += 1)
    {
        CAstStatementCase const* statement_case = &c_ast_statement_cases[index];
        for (u32 layout = 0; layout < BUSTER_ARRAY_LENGTH(c_ast_test_layouts); layout += 1)
        {
            TemporalArena temporary = scratch_begin(&arguments->arena, 1);
            String8 label = string_format(temporary.arena, S8("statement case {u32}, layout {u32}"), index, layout);
            CPreprocessResult preprocess = c_ast_test_preprocess(temporary.arena, statement_case->source, C_PREPROCESS_DIALECT_GNU17);
            CAstResult built = c_ast_build(temporary.arena, preprocess, (CAstOptions){.layout = c_ast_test_layouts[layout]});
            if (BUSTER_REQUIRE(arguments, preprocess.error_count == 0 && built.complete))
            {
                CAst tree = c_ast_test_copy(temporary.arena, &built.ast);
                u32 definition = c_ast_test_find_kind(&tree, C_AST_FUNCTION_DEFINITION);
                u32 statement = c_ast_test_find_kind(&tree, C_AST_EXPRESSION_STATEMENT);
                if (statement_case->tamper == C_AST_STATEMENT_TAMPER_BRACE && BUSTER_REQUIRE(arguments, definition != C_AST_NODE_INVALID))
                {
                    tree.tokens[definition - 1] += 1;
                }
                if (statement_case->tamper == C_AST_STATEMENT_TAMPER_SEMICOLON && BUSTER_REQUIRE(arguments, statement != C_AST_NODE_INVALID))
                {
                    tree.tokens[statement] -= 1;
                }
                u32 list = c_ast_test_find_kind(&tree, C_AST_INITIALIZER_LIST);
                if (statement_case->tamper == C_AST_STATEMENT_TAMPER_LIST && BUSTER_REQUIRE(arguments, list != C_AST_NODE_INVALID))
                {
                    tree.tokens[list] += 1;
                }
                if (statement_case->tamper == C_AST_STATEMENT_TAMPER_DECLARATOR)
                {
                    // `p` and `q`, the fourth and fifth declarators in node order.
                    u32 declarators[5] = {0};
                    u32 found = 0;
                    for (u32 node = 0; node < tree.node_count && found < BUSTER_ARRAY_LENGTH(declarators); node += 1)
                    {
                        declarators[found] = node;
                        found += tree.kinds[node] == C_AST_INIT_DECLARATOR;
                    }
                    if (BUSTER_REQUIRE(arguments, found == BUSTER_ARRAY_LENGTH(declarators)))
                    {
                        tree.tokens[c_ast_subtree_begin(&tree, declarators[4])] = tree.tokens[c_ast_subtree_begin(&tree, declarators[3])];
                    }
                }
                CAnalysisResult plain = c_analyze_semantics_only(temporary.arena, preprocess, c_parse_ast_reference(temporary.arena, preprocess));
                CAstStatementStatistics statistics = {0};
                CParserResult syntax = c_parse_ast_reference(temporary.arena, preprocess);
                syntax.ast = &tree;
                syntax.ast_statement_statistics = &statistics;
                CAnalysisResult planned = c_analyze_semantics_only(temporary.arena, preprocess, syntax);
                BUSTER_TEST_RAW(arguments, plain.diagnostic_count == 0 && planned.diagnostic_count == 0, label);
                String8 difference = c_ast_corpus_bindings_differ(temporary.arena, &plain, &planned);
                BUSTER_TEST_RAW(arguments, difference.length == 0, string_format(temporary.arena, S8("{S8}: {S8}"), label, difference));
                bool followed = statement_case->reason == C_AST_STATEMENT_FALLBACK_NONE;
                BUSTER_TEST_RAW(arguments, statistics.bodies == (u64)followed && statistics.fallbacks == (u64)!followed, label);
                BUSTER_TEST_RAW(arguments,
                                followed || (statistics.reason == statement_case->reason && statistics.fallback_counts[statement_case->reason] == 1 &&
                                             statistics.segments == 0 && statistics.declaration_hints == 0 && statistics.loop_hints == 0),
                                label);
                BUSTER_TEST_RAW(arguments,
                                !followed || (statistics.segments >= statement_case->segments &&
                                              statistics.declaration_hints >= statement_case->declaration_hints &&
                                              statistics.loop_hints >= statement_case->loop_hints && statistics.declines >= statement_case->declines &&
                                              statistics.uses && statistics.bails == statement_case->bails),
                                label);
                BUSTER_TEST_RAW(arguments,
                                !followed || (statistics.declarator_hints >= statement_case->declarator_hints &&
                                              statistics.initializer_hints >= statement_case->initializer_hints &&
                                              statistics.declarator_declines == statement_case->declarator_declines &&
                                              statistics.braced == statement_case->braced),
                                label);
            }
            c_ast_release(&built.ast);
            c_test_scratch_end(temporary);
        }
    }
    // An empty tree has no function bodies: every body is left to the token walk.
    {
        TemporalArena temporary = scratch_begin(&arguments->arena, 1);
        CPreprocessResult preprocess = c_ast_test_preprocess(temporary.arena, S8("int f(int a) { return a; } int g(void) { return 1; }"),
                                                             C_PREPROCESS_DIALECT_GNU17);
        CAnalysisResult plain = c_analyze_semantics_only(temporary.arena, preprocess, c_parse_ast_reference(temporary.arena, preprocess));
        CAst empty = {0};
        CAstStatementStatistics statistics = {0};
        CParserResult syntax = c_parse_ast_reference(temporary.arena, preprocess);
        syntax.ast = &empty;
        syntax.ast_statement_statistics = &statistics;
        CAnalysisResult planned = c_analyze_semantics_only(temporary.arena, preprocess, syntax);
        BUSTER_TEST(arguments, c_ast_corpus_bindings_differ(temporary.arena, &plain, &planned).length == 0);
        BUSTER_TEST(arguments, statistics.fallbacks == 2 && statistics.fallback_counts[C_AST_STATEMENT_FALLBACK_INPUT] == 2 && statistics.bodies == 0);
        BUSTER_TEST(arguments, string_equal(c_ast_statement_fallback_name(C_AST_STATEMENT_FALLBACK_TOKENS), S8("tokens")));
        c_test_scratch_end(temporary);
    }
    return result;
}

// Lowering's statement walker (c_ir_lower_body_advance in c_gen.c, #4060)
// reads a body's structure from its statement nodes: one body with every
// controlled-substatement shape lowers alike in every layout, and alike with
// the tree the caller passes and with none; a tree whose body or header
// anchor is moved refuses the body with a diagnostic, and no token walk
// answers instead.
#define C_AST_EXTENT_CASE_EVERY                                                                                                                      \
    "struct P { int x; };\n"                                                                                                                         \
    "int g(int v);\n"                                                                                                                                \
    "int f(int a, int b)\n"                                                                                                                          \
    "{\n"                                                                                                                                            \
    "    int r = 0;\n"                                                                                                                               \
    "    if (a) r += 1;\n"                                                                                                                           \
    "    if (a > 1) { r += 2; } else if (b) r -= 1; else { r = g(r); }\n"                                                                            \
    "    if (a) if (b) r += 3; else r -= 3;\n"                                                                                                       \
    "    if (b) (r) = (a);\n"                                                                                                                        \
    "    if (b) ;\n"                                                                                                                                 \
    "    while (r > 100) r /= 2;\n"                                                                                                                  \
    "    for (int i = 0; i < a; i++) { if (i == b) continue; r += i; }\n"                                                                            \
    "    for (;;) break;\n"                                                                                                                          \
    "    do r += 1; while (r < 3);\n"                                                                                                                \
    "    if (a < 0) do { r -= 1; } while (r > 0);\n"                                                                                                 \
    "    if (b < 0) do r -= 2; while (r > 4);\n"                                                                                                     \
    "    switch (a) { case 1: r += 1; break; case 2: { r += 2; } default: r += 3; }\n"                                                               \
    "    switch (b) case 4: r += 4;\n"                                                                                                               \
    "    switch (a) { case 5: if (b) case 6: { r += 6; } }\n"                                                                                        \
    "    if (a == 7) [[maybe_unused]] r += 7;\n"                                                                                                     \
    "    if (a == 8) goto out;\n"                                                                                                                    \
    "    if (a == 9) label: r += 9;\n"                                                                                                               \
    "    if (b == 9) return r + 9;\n"                                                                                                                \
    "    r += ({ int t = 0; if (a) t = 1; t; });\n"                                                                                                  \
    "out:\n"                                                                                                                                         \
    "    while (r < 0) { if (b) return 1; else r += 1; }\n"                                                                                          \
    "    return r;\n"                                                                                                                                \
    "}\n"

typedef enum CAstExtentTamper
{
    C_AST_EXTENT_TAMPER_NONE,
    // The body's `{` moves, so no function definition owns the body.
    C_AST_EXTENT_TAMPER_BRACE,
    // An `if` anchor moves off its keyword: the token after it is not `(`.
    C_AST_EXTENT_TAMPER_HEADER,
    C_AST_EXTENT_TAMPER_COUNT,
} CAstExtentTamper;

BUSTER_GLOBAL_LOCAL u64 c_ast_test_instruction_total(CIRLowerResult const* lowered)
{
    u64 total = 0;
    for (u32 module_index = 0; lowered->program && module_index < lowered->program->module_count; module_index += 1)
    {
        IrModule const* module = lowered->program->modules + module_index;
        for (u32 function_index = 0; function_index < module->function_count; function_index += 1)
        {
            total += ((u64)module->functions[function_index].instruction_count << 20) + module->functions[function_index].block_count;
        }
    }
    return total;
}

BUSTER_GLOBAL_LOCAL UnitTestResult c_ast_test_assembly_nodes(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    TemporalArena temporary = scratch_begin(&arguments->arena, 1);
    String8 source = string_format(temporary.arena,
        S8("asm(\"nop\"); int f(int x) {{ register int r asm(\"{S8}\") = x;"
           " asm goto __inline __volatile(\"\" : [out] \"+r\" (r) : [in] \"r\" (x) : \"cc\" : done);"
           " done: return r; }}"), cpu_arch_is_x86(target_native.cpu_arch) ? S8("eax") : S8("x0"));
    CPreprocessResult preprocess = c_ast_test_preprocess(temporary.arena, source, C_PREPROCESS_DIALECT_GNU17);
    CAnalysisResult analysis = c_parse_for_lowering(temporary.arena, preprocess);
    if (BUSTER_REQUIRE(arguments, !preprocess.error_count && analysis.analysis_complete && !analysis.diagnostic_count))
    {
        CIRLowerResult ordinary = c_lower_to_ir(temporary.arena, S8("asm-node-provenance.c"), preprocess, analysis, target_native);
        CPreprocessResult poisoned = preprocess;
        poisoned.tokens = arena_allocate(temporary.arena, CToken, preprocess.token_count);
        memcpy(poisoned.tokens, preprocess.tokens, sizeof(CToken) * preprocess.token_count);
        CAst const* ast = analysis.ast;
        u32 changed = 0;
        for (u32 node = 0; node < ast->node_count; node += 1)
        {
            u32 kind = ast->kinds[node];
            if (kind == C_AST_ASM || kind == C_AST_ASM_TOP_LEVEL || kind == C_AST_ASM_LABEL)
            {
                u32 first = ast->tokens[node];
                // The fixture's asm expressions are single identifiers, and
                // each asm ends at this first semicolon. This test-only scan
                // locates the grammar bytes to corrupt after analysis.
                u32 end = first;
                while (end < preprocess.token_count && preprocess.tokens[end].punctuator != C_PUNCTUATOR_SEMICOLON) end += 1;
                for (u32 index = first; index < end; index += 1)
                {
                    String8 spelling = c_token_spelling(preprocess.spelling_base, preprocess.tokens[index]);
                    if (!string_equal(spelling, S8("r")) && !string_equal(spelling, S8("x")))
                    {
                        poisoned.tokens[index].kind = C_TOKEN_INVALID;
                        poisoned.tokens[index].punctuator = C_PUNCTUATOR_NONE;
                        changed += 1;
                    }
                }
            }
        }
        BUSTER_TEST(arguments, changed >= 30);
        CIRLowerResult retained = c_lower_to_ir(temporary.arena, S8("asm-node-provenance.c"), poisoned, analysis, target_native);
        BUSTER_TEST(arguments, !ordinary.diagnostic_count && ordinary.canonical_ir_certified);
        BUSTER_TEST(arguments, !retained.diagnostic_count && retained.canonical_ir_certified);
        BUSTER_TEST(arguments, c_ast_test_instruction_total(&ordinary) == c_ast_test_instruction_total(&retained));
        for (u32 form = 0; form < 2; form += 1)
        {
            CIRLowerResult const* lowered = form ? &retained : &ordinary;
            if (BUSTER_REQUIRE(arguments, lowered->program && lowered->program->module_count == 1))
            {
                IrModule* module = lowered->program->modules;
                BUSTER_TEST(arguments, module->assembly_count == 1 && string_equal(module->assemblies[0].source, S8("nop")));
                u32 assemblies = 0;
                for (u32 function = 0; function < module->function_count; function += 1)
                {
                    IrFunction* body = module->functions + function;
                    for (u32 index = 0; index < body->instruction_count; index += 1)
                    {
                        IrInstruction* instruction = body->instructions + index;
                        if (instruction->opcode == IR_OPCODE_INLINE_ASSEMBLY)
                        {
                            IrInstructionExtra extra = ir_instruction_extra(body, (IrInstructionId){.value = index});
                            assemblies += 1;
                            BUSTER_TEST(arguments, instruction->volatile_access && instruction->operand_count == 2 && instruction->target_count == 2);
                            BUSTER_TEST(arguments, instruction->immediate_count == 2 &&
                                                   (instruction->immediates[0] & IR_INLINE_ASSEMBLY_CONSTRAINT_OUTPUT) &&
                                                   (instruction->immediates[0] & IR_INLINE_ASSEMBLY_CONSTRAINT_READ_WRITE));
                            BUSTER_TEST(arguments, cpu_arch_is_x86(target_native.cpu_arch)
                                ? (instruction->immediates[0] & IR_INLINE_ASSEMBLY_CONSTRAINT_CLASS_MASK) == IR_INLINE_ASSEMBLY_CONSTRAINT_A
                                : IR_INLINE_ASSEMBLY_CONSTRAINT_HAS_PHYSICAL_REGISTER(instruction->immediates[0]) &&
                                  IR_INLINE_ASSEMBLY_CONSTRAINT_PHYSICAL_REGISTER_INDEX(instruction->immediates[0]) == 0);
                            BUSTER_TEST(arguments, extra.operand_name_count == 2 && string_equal(extra.operand_names[0], S8("out")) &&
                                                   string_equal(extra.operand_names[1], S8("in")));
                            BUSTER_TEST(arguments, extra.clobber_count == 1 && string_equal(extra.clobbers[0], S8("cc")));
                            BUSTER_TEST(arguments, extra.label_name_count == 1 && string_equal(extra.label_names[0], S8("done")));
                        }
                    }
                }
                BUSTER_TEST(arguments, assemblies == 1);
            }
        }
    }
    String8 invalid[] = {
        S8("int f(void) { asm goto __inline __volatile(\"\" : : : : missing); return 0; }"),
        S8("int f(void) { asm goto __inline __volatile(\"\" : : : : done); done: goto missing; }"),
        S8("int f(void) { asm(\"\" : : \"r\" (({ goto missing; 0; }))); return 0; }"),
    };
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(invalid); index += 1)
    {
        CPreprocessResult rejected = c_ast_test_preprocess(temporary.arena, invalid[index], C_PREPROCESS_DIALECT_GNU17);
        CAnalysisResult checked = c_parse_for_lowering(temporary.arena, rejected);
        BUSTER_TEST(arguments, !rejected.error_count && checked.diagnostic_count && checked.lowering_validated);
        bool undefined = false;
        for (u32 diagnostic = 0; diagnostic < checked.diagnostic_count; diagnostic += 1)
        {
            String8 message = checked.diagnostics[diagnostic].message;
            undefined |= string_first_sequence(message, S8("label 'missing' is not defined")) < message.length;
        }
        BUSTER_TEST(arguments, undefined);
        CIRLowerResult failed = c_lower_to_ir(temporary.arena, S8("asm-invalid-label.c"), rejected, checked, target_native);
        BUSTER_TEST(arguments, failed.diagnostic_count && !failed.canonical_ir_certified);
    }
    c_test_scratch_end(temporary);
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult c_ast_test_statement_nodes(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    for (u32 tamper = 0; tamper < C_AST_EXTENT_TAMPER_COUNT; tamper += 1)
    {
        for (u32 layout = 0; layout < BUSTER_ARRAY_LENGTH(c_ast_test_layouts); layout += 1)
        {
            TemporalArena temporary = scratch_begin(&arguments->arena, 1);
            String8 label = string_format(temporary.arena, S8("statement nodes, tamper {u32}, layout {u32}"), tamper, layout);
            CPreprocessResult preprocess = c_ast_test_preprocess(temporary.arena, S8(C_AST_EXTENT_CASE_EVERY), C_PREPROCESS_DIALECT_GNU17);
            CAstResult built = c_ast_build(temporary.arena, preprocess, (CAstOptions){.layout = c_ast_test_layouts[layout]});
            if (BUSTER_REQUIRE(arguments, preprocess.error_count == 0 && built.complete))
            {
                CAst tree = c_ast_test_copy(temporary.arena, &built.ast);
                u32 definition = c_ast_test_find_kind(&tree, C_AST_FUNCTION_DEFINITION);
                u32 header = c_ast_test_find_kind(&tree, C_AST_IF);
                if (tamper == C_AST_EXTENT_TAMPER_BRACE && BUSTER_REQUIRE(arguments, definition != C_AST_NODE_INVALID))
                {
                    tree.tokens[definition - 1] += 1;
                }
                if (tamper == C_AST_EXTENT_TAMPER_HEADER && BUSTER_REQUIRE(arguments, header != C_AST_NODE_INVALID))
                {
                    tree.tokens[header] += 1;
                }
                CAnalysisResult analysis = c_parse_for_lowering(temporary.arena, preprocess);
                if (BUSTER_REQUIRE(arguments, analysis.analysis_complete && analysis.diagnostic_count == 0))
                {
                    // No tree passed: lowering reuses the retained unit tree.
                    CIRLowerResult own = c_lower_to_ir_with_options(temporary.arena, label, preprocess, analysis, target_native, (CIRLowerOptions){0});
                    CIRLowerResult passed = c_lower_to_ir_with_options(temporary.arena, label, preprocess, analysis, target_native,
                                                                       (CIRLowerOptions){.syntax_tree = &tree});
                    BUSTER_TEST_RAW(arguments, own.program && own.diagnostic_count == 0 && own.canonical_ir_certified, label);
                    if (tamper == C_AST_EXTENT_TAMPER_NONE)
                    {
                        BUSTER_TEST_RAW(arguments, passed.program && passed.diagnostic_count == 0 && passed.canonical_ir_certified, label);
                        BUSTER_TEST_RAW(arguments, c_ast_test_instruction_total(&passed) == c_ast_test_instruction_total(&own), label);
                    }
                    else
                    {
                        bool refused = passed.diagnostic_count == 1 && !passed.canonical_ir_certified &&
                                       string_starts_with_sequence(passed.diagnostics[0].message, S8("in function 'f': "));
                        BUSTER_TEST_RAW(arguments, refused, label);
                        if (tamper == C_AST_EXTENT_TAMPER_BRACE && refused)
                        {
                            BUSTER_TEST_RAW(arguments, string_equal(passed.diagnostics[0].message, S8("in function 'f': function body has no syntax tree node")),
                                            passed.diagnostics[0].message);
                        }
                    }
                }
            }
            c_ast_release(&built.ast);
            c_test_scratch_end(temporary);
        }
    }
    // Without a caller override lowering reuses the analyzed unit's tree.
    {
        TemporalArena temporary = scratch_begin(&arguments->arena, 1);
        CPreprocessResult preprocess = c_ast_test_preprocess(temporary.arena, S8("int f(int a) { if (a) return 1; return 0; }"), C_PREPROCESS_DIALECT_GNU17);
        CAnalysisResult analysis = c_parse_for_lowering(temporary.arena, preprocess);
        CAstLowerStatistics lower = {0};
        CIRLowerResult lowered = c_lower_to_ir_with_options(temporary.arena, S8("statement nodes, no tree"), preprocess, analysis, target_native,
                                                            (CIRLowerOptions){.ast_lower_statistics = &lower});
        BUSTER_TEST_RAW(arguments,
                        lowered.program && lower.statement_bodies == 1 && lower.trees_built == 0 && lower.tree_rejections == 0 && analysis.ast &&
                            analysis.ast->node_count != 0 && analysis.frontend_retained_copy_bytes != 0 && lower.span_nodes != 0 && lower.span_bytes != 0,
                        string_format(temporary.arena, S8("statement nodes, no tree: program={u32} bodies={u64} trees={u64} rejections={u64} nodes={u64} spans={u64}"),
                                      (u32)(lowered.program != 0), lower.statement_bodies, lower.trees_built, lower.tree_rejections, lower.tree_nodes,
                                      lower.span_nodes));
        c_test_scratch_end(temporary);
    }
    return result;
}
// Each test ends its scratch with c_test_scratch_end, so the module's address
// space is bounded by the units one fixture holds at once, not by how many
// sources it preprocesses; C_TEST_BOUNDED_FIXTURE checks it (GitHub #3306).

// The declaration specifier read (c_parse.c, #3102) on chosen units, in every
// layout: one whose block, file-scope and parameter specifier runs are all
// read from the tree, and one shape per CAstSpecifierFallback -- a tree whose
// specifier list does not start at the run's first token, a tag and an
// attribute (lists that are not word-only), `_Atomic` among the words,
// conflicting storage classes, a word the tree calls a type word where the
// scan finds none, and a restrict-qualified typedef under a qualifier. Each
// analysis with the tree must equal the analysis without it, model and
// diagnostics alike. Last, tags nested a thousand deep must build the index
// in one read of each list's children.
typedef enum CAstSpecifierTamper
{
    C_AST_SPECIFIER_TAMPER_NONE,
    // Move the first DECL_SPECIFIERS node off its first token.
    C_AST_SPECIFIER_TAMPER_TOKEN,
    // Call the first SPECIFIER_WORD `int`.
    C_AST_SPECIFIER_TAMPER_WORD,
} CAstSpecifierTamper;

typedef struct CAstSpecifierCase CAstSpecifierCase;
struct CAstSpecifierCase
{
    String8 source;
    CAstSpecifierTamper tamper;
    CAstSpecifierFallback reason;
    u32 primitive_answers;
    u32 typedef_answers;
    // The machine diagnoses the unit (with the tree and without it alike).
    bool diagnosed;
};

BUSTER_GLOBAL_LOCAL CAstSpecifierCase const c_ast_specifier_cases[] = {
    {S8_INITIALIZER("typedef unsigned long U;\n"
                    "static const U g = 1;\n"
                    "extern volatile int e;\n"
                    "_Thread_local static int t;\n"
                    "inline static int h(const U a, register int b, U c)\n"
                    "{\n"
                    "    static volatile long long s;\n"
                    "    const U d = a;\n"
                    "    unsigned char k = 2;\n"
                    "    U const m = c, n = d;\n"
                    "    return (int)(s + d + (U)b + k + m + n);\n"
                    "}\n"),
     C_AST_SPECIFIER_TAMPER_NONE, C_AST_SPECIFIER_FALLBACK_NONE, 7, 5, false},
    {S8_INITIALIZER("int x;\n"), C_AST_SPECIFIER_TAMPER_TOKEN, C_AST_SPECIFIER_FALLBACK_INPUT, 0, 0, false},
    {S8_INITIALIZER("struct S { int a; };\nconst struct S s;\n"), C_AST_SPECIFIER_TAMPER_NONE, C_AST_SPECIFIER_FALLBACK_INPUT, 0, 0, false},
    {S8_INITIALIZER("int __attribute__((unused)) x;\n"), C_AST_SPECIFIER_TAMPER_NONE, C_AST_SPECIFIER_FALLBACK_INPUT, 0, 0, false},
    {S8_INITIALIZER("_Atomic int x;\n"), C_AST_SPECIFIER_TAMPER_NONE, C_AST_SPECIFIER_FALLBACK_SPECIFIERS, 0, 0, false},
    {S8_INITIALIZER("static extern int x;\n"), C_AST_SPECIFIER_TAMPER_NONE, C_AST_SPECIFIER_FALLBACK_STORAGE, 0, 0, true},
    {S8_INITIALIZER("static x;\n"), C_AST_SPECIFIER_TAMPER_WORD, C_AST_SPECIFIER_FALLBACK_PRIMITIVE, 0, 0, true},
    {S8_INITIALIZER("typedef int *restrict RP;\nconst RP p;\n"), C_AST_SPECIFIER_TAMPER_NONE, C_AST_SPECIFIER_FALLBACK_TYPEDEF, 1, 0, false},
};

BUSTER_GLOBAL_LOCAL UnitTestResult c_ast_test_specifiers(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(c_ast_specifier_cases); index += 1)
    {
        CAstSpecifierCase const* specifier_case = &c_ast_specifier_cases[index];
        for (u32 layout = 0; layout < BUSTER_ARRAY_LENGTH(c_ast_test_layouts); layout += 1)
        {
            TemporalArena temporary = scratch_begin(&arguments->arena, 1);
            String8 label = string_format(temporary.arena, S8("specifier case {u32}, layout {u32}"), index, layout);
            CPreprocessResult preprocess = c_ast_test_preprocess(temporary.arena, specifier_case->source, C_PREPROCESS_DIALECT_GNU17);
            CAstResult built = c_ast_build(temporary.arena, preprocess, (CAstOptions){.layout = c_ast_test_layouts[layout]});
            if (BUSTER_REQUIRE(arguments, preprocess.error_count == 0 && built.complete))
            {
                CAst tree = c_ast_test_copy(temporary.arena, &built.ast);
                u32 specifiers = c_ast_test_find_kind(&tree, C_AST_DECL_SPECIFIERS);
                u32 word = c_ast_test_find_kind(&tree, C_AST_SPECIFIER_WORD);
                if (specifier_case->tamper == C_AST_SPECIFIER_TAMPER_TOKEN && BUSTER_REQUIRE(arguments, specifiers != C_AST_NODE_INVALID))
                {
                    tree.tokens[specifiers] += 1;
                }
                if (specifier_case->tamper == C_AST_SPECIFIER_TAMPER_WORD && BUSTER_REQUIRE(arguments, word != C_AST_NODE_INVALID))
                {
                    tree.data[word] = C_AST_WORD_INT;
                }
                CAnalysisResult plain = c_analyze_semantics_only(temporary.arena, preprocess, c_parse_ast_reference(temporary.arena, preprocess));
                CAstSpecifierStatistics statistics = {0};
                CParserResult syntax = c_parse_ast_reference(temporary.arena, preprocess);
                syntax.ast = &tree;
                syntax.ast_specifier_statistics = &statistics;
                CAnalysisResult read = c_analyze_semantics_only(temporary.arena, preprocess, syntax);
                BUSTER_TEST_RAW(arguments, (plain.diagnostic_count != 0) == specifier_case->diagnosed, label);
                String8 difference = c_ast_corpus_analyses_differ(temporary.arena, &plain, &read);
                difference = difference.length ? difference : c_ast_corpus_model_differ(temporary.arena, &plain, &read);
                BUSTER_TEST_RAW(arguments, difference.length == 0, string_format(temporary.arena, S8("{S8}: {S8}"), label, difference));
                bool read_all = specifier_case->reason == C_AST_SPECIFIER_FALLBACK_NONE;
                BUSTER_TEST_RAW(arguments,
                                statistics.primitive_answers == specifier_case->primitive_answers &&
                                    statistics.typedef_answers == specifier_case->typedef_answers,
                                string_format(temporary.arena, S8("{S8}: {u64} primitive and {u64} typedef answers"), label,
                                              statistics.primitive_answers, statistics.typedef_answers));
                BUSTER_TEST_RAW(arguments,
                                read_all ? statistics.fallbacks == 0
                                         : statistics.fallbacks == 1 && statistics.reason == specifier_case->reason &&
                                               statistics.fallback_counts[specifier_case->reason] == 1,
                                string_format(temporary.arena, S8("{S8}: {u64} fallbacks, the last '{S8}'"), label, statistics.fallbacks,
                                              c_ast_specifier_fallback_name(statistics.reason)));
                BUSTER_TEST_RAW(arguments, statistics.lists != 0, label);
            }
            c_ast_release(&built.ast);
            c_test_scratch_end(temporary);
        }
    }
    // The test seam turns the read off: no list is indexed and every run goes
    // to the machine without being counted.
    {
        TemporalArena temporary = scratch_begin(&arguments->arena, 1);
        CPreprocessResult preprocess = c_ast_test_preprocess(temporary.arena, S8("int f(int a) { int b = a; return b; }"), C_PREPROCESS_DIALECT_GNU17);
        CAstResult built = c_ast_build(temporary.arena, preprocess, (CAstOptions){0});
        if (BUSTER_REQUIRE(arguments, built.complete))
        {
            CAstSpecifierStatistics statistics = {0};
            CParserResult syntax = c_parse_ast_reference(temporary.arena, preprocess);
            syntax.ast = &built.ast;
            syntax.ast_specifier_statistics = &statistics;
            c_test_ast_specifiers_set(false);
            c_analyze_semantics_only(temporary.arena, preprocess, syntax);
            c_test_ast_specifiers_set(true);
            BUSTER_TEST(arguments, statistics.lists == 0 && statistics.primitive_answers == 0 && statistics.fallbacks == 0);
        }
        c_ast_release(&built.ast);
        BUSTER_TEST(arguments, string_equal(c_ast_specifier_fallback_name(C_AST_SPECIFIER_FALLBACK_PRIMITIVE), S8("primitive")));
        c_test_scratch_end(temporary);
    }
    // `struct S0 { struct S1 { ... int x; } m1; ... } m0;`: postorder closes
    // each enclosing list after every list it holds, so an index sorted as it
    // is built moved each list past all the inner ones, quadratic in the
    // depth. The index holds only the one word-only list and reads every
    // list's children at most once.
    {
        TemporalArena temporary = scratch_begin(&arguments->arena, 1);
        u32 const depth = 1000;
        u64 capacity = (u64)depth * 40 + 64;
        char8* text = arena_allocate(temporary.arena, char8, capacity);
        u64 length = 0;
        for (u32 level = 0; level < depth; level += 1)
        {
            // Braces are written apart from the format, which reads `{` as a field.
            String8 parts[] = {string_format(temporary.arena, S8("struct S{u32} "), level), S8("{\n")};
            for (u32 part = 0; part < BUSTER_ARRAY_LENGTH(parts); part += 1)
            {
                memcpy(text + length, parts[part].pointer, parts[part].length);
                length += parts[part].length;
            }
        }
        String8 inner = S8("int x;\n");
        memcpy(text + length, inner.pointer, inner.length);
        length += inner.length;
        for (u32 level = depth; level > 0; level -= 1)
        {
            String8 parts[] = {S8("}"), string_format(temporary.arena, S8(" m{u32};\n"), level - 1)};
            for (u32 part = 0; part < BUSTER_ARRAY_LENGTH(parts); part += 1)
            {
                memcpy(text + length, parts[part].pointer, parts[part].length);
                length += parts[part].length;
            }
        }
        CPreprocessResult preprocess = c_ast_test_preprocess(temporary.arena, (String8){.pointer = text, .length = length}, C_PREPROCESS_DIALECT_GNU17);
        CAstResult built = c_ast_build(temporary.arena, preprocess, (CAstOptions){0});
        if (BUSTER_REQUIRE(arguments, preprocess.error_count == 0 && built.complete))
        {
            CAnalysisResult plain = c_analyze_semantics_only(temporary.arena, preprocess, c_parse_ast_reference(temporary.arena, preprocess));
            CAstSpecifierStatistics statistics = {0};
            CParserResult syntax = c_parse_ast_reference(temporary.arena, preprocess);
            syntax.ast = &built.ast;
            syntax.ast_specifier_statistics = &statistics;
            CAnalysisResult read = c_analyze_semantics_only(temporary.arena, preprocess, syntax);
            String8 difference = c_ast_corpus_analyses_differ(temporary.arena, &plain, &read);
            difference = difference.length ? difference : c_ast_corpus_model_differ(temporary.arena, &plain, &read);
            BUSTER_TEST_RAW(arguments, difference.length == 0, difference);
            BUSTER_TEST_RAW(arguments,
                            statistics.lists == depth + 1 && statistics.indexed == 1 && statistics.index_reads <= built.ast.node_count &&
                                statistics.fallbacks == 1 && statistics.fallback_counts[C_AST_SPECIFIER_FALLBACK_INPUT] == 1,
                            string_format(temporary.arena, S8("nested tags: {u64} lists, {u64} indexed, {u64} reads over {u32} nodes, {u64} fallbacks"),
                                          statistics.lists, statistics.indexed, statistics.index_reads, built.ast.node_count, statistics.fallbacks));
        }
        c_ast_release(&built.ast);
        c_test_scratch_end(temporary);
    }
    return result;
}

// The type-name runs (c_parse_type_name_specifiers, #3102) on chosen units, in
// every layout: runs skipped as no type name, one of them holding a `typeof`
// aggregate whose body the failure path's scan finds already defined (no
// input tried defines one there, so none is retried), runs read from the
// tree, and one
// shape per CAstSpecifierFallback a type name reaches -- a tag, `_Atomic`
// among the words, storage-class words and a type word the tree claims where
// the source holds others, and a restrict-qualified typedef under a
// qualifier. Each analysis with the tree must equal the analysis without it,
// model and diagnostics alike, and verify mode must find the machine silent
// on every skipped run. The last case is the mutation check: a tree whose
// type name's list is moved off its first token makes the skip wrong, and
// verify mode must catch the machine answering there.
typedef enum CAstTypeNameTamper
{
    C_AST_TYPE_NAME_TAMPER_NONE,
    // Move the type name's DECL_SPECIFIERS node off its first token.
    C_AST_TYPE_NAME_TAMPER_TOKEN,
    // Call the type name's first two words `static` and `extern`.
    C_AST_TYPE_NAME_TAMPER_STORAGE,
    // Make the type name's TYPEDEF_NAME the word `int`.
    C_AST_TYPE_NAME_TAMPER_KIND,
} CAstTypeNameTamper;

typedef struct CAstTypeNameCase CAstTypeNameCase;
struct CAstTypeNameCase
{
    String8 source;
    CAstTypeNameTamper tamper;
    // The reason of the one run handed to the machine, or NONE when none is.
    CAstSpecifierFallback reason;
    u32 skips;
    u32 retries;
    u32 primitive_answers;
    u32 typedef_answers;
    // Skipped runs verify mode finds the machine answering: only a tampered
    // tree has any.
    u32 answered;
};

BUSTER_GLOBAL_LOCAL CAstTypeNameCase const c_ast_type_name_cases[] = {
    {S8_INITIALIZER("typedef unsigned long U;\n"
                    "unsigned long f(void)\n"
                    "{\n"
                    "    return sizeof (U) + sizeof (const U) + sizeof (unsigned int) + _Alignof (U) + (U){1};\n"
                    "}\n"),
     C_AST_TYPE_NAME_TAMPER_NONE, C_AST_SPECIFIER_FALLBACK_NONE, 0, 0, 1, 4, 0},
    {S8_INITIALIZER("unsigned long f(int a)\n"
                    "{\n"
                    "    if (!a)\n"
                    "    {\n"
                    "        return sizeof (a + 1);\n"
                    "    }\n"
                    "    return 0;\n"
                    "}\n"),
     C_AST_TYPE_NAME_TAMPER_NONE, C_AST_SPECIFIER_FALLBACK_NONE, 2, 0, 0, 0, 0},
    {S8_INITIALIZER("unsigned long f(void)\n"
                    "{\n"
                    "    return sizeof ((__typeof__(struct { int a; }) *)0);\n"
                    "}\n"),
     C_AST_TYPE_NAME_TAMPER_NONE, C_AST_SPECIFIER_FALLBACK_NONE, 1, 0, 0, 0, 0},
    {S8_INITIALIZER("struct S { int x; };\nunsigned long f(void)\n{\n    return sizeof (struct S);\n}\n"), C_AST_TYPE_NAME_TAMPER_NONE,
     C_AST_SPECIFIER_FALLBACK_INPUT, 0, 0, 0, 0, 0},
    {S8_INITIALIZER("unsigned long f(void)\n{\n    return sizeof (_Atomic int);\n}\n"), C_AST_TYPE_NAME_TAMPER_NONE,
     C_AST_SPECIFIER_FALLBACK_SPECIFIERS, 0, 0, 0, 0, 0},
    {S8_INITIALIZER("unsigned long f(void)\n{\n    return sizeof (const volatile int);\n}\n"), C_AST_TYPE_NAME_TAMPER_STORAGE,
     C_AST_SPECIFIER_FALLBACK_STORAGE, 0, 0, 0, 0, 0},
    {S8_INITIALIZER("typedef int T;\nunsigned long f(void)\n{\n    return sizeof (const T);\n}\n"), C_AST_TYPE_NAME_TAMPER_KIND,
     C_AST_SPECIFIER_FALLBACK_PRIMITIVE, 0, 0, 0, 0, 0},
    {S8_INITIALIZER("typedef int *restrict RP;\nunsigned long f(void)\n{\n    return sizeof (const RP);\n}\n"), C_AST_TYPE_NAME_TAMPER_NONE,
     C_AST_SPECIFIER_FALLBACK_TYPEDEF, 0, 0, 0, 0, 0},
    {S8_INITIALIZER("typedef unsigned long U;\nunsigned long f(void)\n{\n    return sizeof (U);\n}\n"), C_AST_TYPE_NAME_TAMPER_TOKEN,
     C_AST_SPECIFIER_FALLBACK_NONE, 1, 0, 0, 0, 1},
};

BUSTER_GLOBAL_LOCAL UnitTestResult c_ast_test_type_names(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(c_ast_type_name_cases); index += 1)
    {
        CAstTypeNameCase const* name_case = &c_ast_type_name_cases[index];
        for (u32 layout = 0; layout < BUSTER_ARRAY_LENGTH(c_ast_test_layouts); layout += 1)
        {
            TemporalArena temporary = scratch_begin(&arguments->arena, 1);
            String8 label = string_format(temporary.arena, S8("type-name case {u32}, layout {u32}"), index, layout);
            CPreprocessResult preprocess = c_ast_test_preprocess(temporary.arena, name_case->source, C_PREPROCESS_DIALECT_GNU17);
            CAstResult built = c_ast_build(temporary.arena, preprocess, (CAstOptions){.layout = c_ast_test_layouts[layout]});
            if (BUSTER_REQUIRE(arguments, preprocess.error_count == 0 && built.complete))
            {
                CAst tree = c_ast_test_copy(temporary.arena, &built.ast);
                u32 type_name = c_ast_test_find_kind(&tree, C_AST_TYPE_NAME);
                u32 specifiers = type_name != C_AST_NODE_INVALID ? c_ast_child_at(&tree, type_name, 0) : C_AST_NODE_INVALID;
                if (name_case->tamper != C_AST_TYPE_NAME_TAMPER_NONE &&
                    BUSTER_REQUIRE(arguments, specifiers != C_AST_NODE_INVALID && tree.kinds[specifiers] == C_AST_DECL_SPECIFIERS))
                {
                    u32 first = c_ast_subtree_begin(&tree, specifiers);
                    if (name_case->tamper == C_AST_TYPE_NAME_TAMPER_TOKEN)
                    {
                        tree.tokens[specifiers] += 1;
                    }
                    else if (name_case->tamper == C_AST_TYPE_NAME_TAMPER_STORAGE && BUSTER_REQUIRE(arguments, first + 2 < specifiers))
                    {
                        tree.data[first] = C_AST_WORD_STATIC;
                        tree.data[first + 1] = C_AST_WORD_EXTERN;
                    }
                    else if (name_case->tamper == C_AST_TYPE_NAME_TAMPER_KIND && BUSTER_REQUIRE(arguments, tree.kinds[specifiers - 1] == C_AST_TYPEDEF_NAME))
                    {
                        tree.kinds[specifiers - 1] = C_AST_SPECIFIER_WORD;
                        tree.data[specifiers - 1] = C_AST_WORD_INT;
                    }
                }
                CAnalysisResult plain = c_analyze_semantics_only(temporary.arena, preprocess, c_parse_ast_reference(temporary.arena, preprocess));
                CAstSpecifierStatistics statistics = {0};
                CParserResult syntax = c_parse_ast_reference(temporary.arena, preprocess);
                syntax.ast = &tree;
                syntax.ast_specifier_statistics = &statistics;
                CAnalysisResult read = c_analyze_semantics_only(temporary.arena, preprocess, syntax);
                BUSTER_TEST_RAW(arguments, plain.diagnostic_count == 0, label);
                String8 difference = c_ast_corpus_analyses_differ(temporary.arena, &plain, &read);
                difference = difference.length ? difference : c_ast_corpus_model_differ(temporary.arena, &plain, &read);
                difference = difference.length ? difference : c_ast_corpus_bindings_differ(temporary.arena, &plain, &read);
                BUSTER_TEST_RAW(arguments, difference.length == 0, string_format(temporary.arena, S8("{S8}: {S8}"), label, difference));
                bool handed = name_case->reason != C_AST_SPECIFIER_FALLBACK_NONE;
                BUSTER_TEST_RAW(arguments,
                                statistics.type_name_skips == name_case->skips && statistics.type_name_retries == name_case->retries &&
                                    statistics.type_name_primitive_answers == name_case->primitive_answers &&
                                    statistics.type_name_typedef_answers == name_case->typedef_answers &&
                                    statistics.type_name_fallbacks == (u64)handed && (!handed || statistics.type_name_fallback_counts[name_case->reason] == 1),
                                string_format(temporary.arena,
                                              S8("{S8}: {u64} skips ({u64} retried), {u64} primitive and {u64} typedef answers, {u64} fallbacks"), label,
                                              statistics.type_name_skips, statistics.type_name_retries, statistics.type_name_primitive_answers,
                                              statistics.type_name_typedef_answers, statistics.type_name_fallbacks));
                CParserResult verified = c_parse_ast_reference(temporary.arena, preprocess);
                verified.ast = &tree;
                c_test_ast_type_names_verify_take();
                c_test_ast_type_names_verify_set(true);
                c_analyze_semantics_only(temporary.arena, preprocess, verified);
                c_test_ast_type_names_verify_set(false);
                CTestAstTypeNameVerify verify = c_test_ast_type_names_verify_take();
                BUSTER_TEST_RAW(arguments,
                                verify.checked == name_case->skips && verify.answered == name_case->answered && verify.appended == 0 &&
                                    verify.diagnosed == 0,
                                string_format(temporary.arena, S8("{S8}: verify checked {u64}, answered {u64}, appended {u64}, diagnosed {u64}"), label,
                                              verify.checked, verify.answered, verify.appended, verify.diagnosed));
            }
            c_ast_release(&built.ast);
            c_test_scratch_end(temporary);
        }
    }
    return result;
}

// The declaration hints' specifier facts (c_parse_plan_declarators, #3102)
// on chosen bodies, in every layout: storage classes in every position the
// walker folds (a list, a typedef, a statement expression's own static, a
// `for` declaration, an initializer that spells none), and `auto` or
// `__auto_type` words where the auto-inference scan reads them, the
// specifiers and the first declarator, as a parameter's specifier included.
// Each analysis with the tree must equal the analysis without it, diagnostics
// and model rows alike, inline hints and enumerator constants included; and
// the model comparator must report a mutation of either.
BUSTER_GLOBAL_LOCAL UnitTestResult c_ast_test_declaration_hints(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    String8 const sources[] = {
        S8("int g(int);\n"
           "void f(int c)\n"
           "{\n"
           "    static int s = 1, t;\n"
           "    extern int x;\n"
           "    register int r = c;\n"
           "    typedef int T;\n"
           "    T u = s + g(r);\n"
           "    static __thread int w;\n"
           "    int v = ({ static int z = 2; z + t + w; });\n"
           "    for (register int i = 0; i < c; i += 1) { u += i; }\n"
           "    (void)u; (void)v; (void)x;\n"
           "}\n"),
        S8("void f(int c) { __auto_type a = c; auto int b = c; (void)a; (void)b; }\n"),
        S8("void f(void) { int (*h)(__auto_type x); (void)h; }\n"),
        S8("void f(int c) { int (*h)(int), k = c; __typeof__(c) m = k; (void)h; (void)m; }\n"),
        S8("enum E { A = 1, B = -2 };\n"
           "__attribute__((always_inline)) inline int f(void) { int x = A + B; return x; }\n"),
    };
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(sources); index += 1)
    {
        for (u32 layout = 0; layout < BUSTER_ARRAY_LENGTH(c_ast_test_layouts); layout += 1)
        {
            TemporalArena temporary = scratch_begin(&arguments->arena, 1);
            String8 label = string_format(temporary.arena, S8("declaration hint case {u32}, layout {u32}"), index, layout);
            CPreprocessResult preprocess = c_ast_test_preprocess(temporary.arena, sources[index], C_PREPROCESS_DIALECT_GNU17);
            CAstResult built = c_ast_build(temporary.arena, preprocess, (CAstOptions){.layout = c_ast_test_layouts[layout]});
            if (BUSTER_REQUIRE(arguments, preprocess.error_count == 0 && built.complete))
            {
                CAnalysisResult plain = c_analyze_semantics_only(temporary.arena, preprocess, c_parse_ast_reference(temporary.arena, preprocess));
                CAstStatementStatistics statistics = {0};
                CParserResult syntax = c_parse_ast_reference(temporary.arena, preprocess);
                syntax.ast = &built.ast;
                syntax.ast_statement_statistics = &statistics;
                CAnalysisResult planned = c_analyze_semantics_only(temporary.arena, preprocess, syntax);
                String8 difference = c_ast_corpus_analyses_differ(temporary.arena, &plain, &planned);
                u64 rows = 0;
                difference = difference.length ? difference : c_ast_corpus_models_differ(temporary.arena, &plain, &planned, &rows);
                BUSTER_TEST_RAW(arguments, difference.length == 0, string_format(temporary.arena, S8("{S8}: {S8}"), label, difference));
                BUSTER_TEST_RAW(arguments, statistics.bodies == 1 && statistics.fallbacks == 0, label);
            }
            c_ast_release(&built.ast);
            c_test_scratch_end(temporary);
        }
    }
    // The model comparator itself: a copy of one analysis that differs only in
    // an entity's merged inline hints, or in one field of an enumerator's
    // declaration-point integer constant, must be reported by that field.
    {
        TemporalArena temporary = scratch_begin(&arguments->arena, 1);
        CPreprocessResult preprocess = c_ast_test_preprocess(temporary.arena,
                                                             S8("enum E { A = 1, B = -2 };\n"
                                                                "__attribute__((always_inline)) inline int f(void) { return A + B; }\n"),
                                                             C_PREPROCESS_DIALECT_GNU17);
        CAnalysisResult left = c_analyze_semantics_only(temporary.arena, preprocess, c_parse_ast_reference(temporary.arena, preprocess));
        u32 hinted = left.entity_count;
        for (u32 index = 0; index < left.entity_count; index += 1)
        {
            hinted = hinted == left.entity_count && left.entities[index].inline_hints ? index : hinted;
        }
        if (BUSTER_REQUIRE(arguments, left.diagnostic_count == 0 && hinted < left.entity_count && left.enum_member_count == 2 &&
                                          left.enum_members[1].integer_constant.valid))
        {
            String8 const fields[] = {
                S8("inline_hints"),
                S8("integer_constant.magnitude"),
                S8("integer_constant.magnitude_high"),
                S8("integer_constant.type.value"),
                S8("integer_constant.rank"),
                S8("integer_constant.bit_width"),
                S8("integer_constant.is_signed"),
                S8("integer_constant.is_negative"),
                S8("integer_constant.valid"),
            };
            for (u32 mutation = 0; mutation < BUSTER_ARRAY_LENGTH(fields); mutation += 1)
            {
                CAnalysisResult right = left;
                right.entities = arena_allocate(temporary.arena, CEntity, left.entity_count);
                right.enum_members = arena_allocate(temporary.arena, CEnumMember, left.enum_member_count);
                memcpy(right.entities, left.entities, sizeof(CEntity) * left.entity_count);
                memcpy(right.enum_members, left.enum_members, sizeof(CEnumMember) * left.enum_member_count);
                CIntegerConstant* constant = &right.enum_members[1].integer_constant;
                switch (mutation)
                {
                case 0:
                    right.entities[hinted].inline_hints ^= C_ENTITY_INLINE_HINT_ALWAYS;
                    break;
                case 1:
                    constant->magnitude += 1;
                    break;
                case 2:
                    constant->magnitude_high += 1;
                    break;
                case 3:
                    constant->type.value += 1;
                    break;
                case 4:
                    constant->rank = constant->rank == C_INTEGER_RANK_INT ? C_INTEGER_RANK_LONG : C_INTEGER_RANK_INT;
                    break;
                case 5:
                    constant->bit_width += 1;
                    break;
                case 6:
                    constant->is_signed = !constant->is_signed;
                    break;
                case 7:
                    constant->is_negative = !constant->is_negative;
                    break;
                default:
                    constant->valid = !constant->valid;
                    break;
                }
                u64 rows = 0;
                String8 difference = c_ast_corpus_models_differ(temporary.arena, &left, &right, &rows);
                String8 expected = mutation == 0 ? string_format(temporary.arena, S8("entities {u32} differs in {S8}"), hinted, fields[mutation])
                                                 : string_format(temporary.arena, S8("enum_members 1 differs in {S8}"), fields[mutation]);
                BUSTER_TEST_RAW(arguments, string_equal(difference, expected),
                                string_format(temporary.arena, S8("model mutation '{S8}': '{S8}'"), fields[mutation], difference));
            }
        }
        c_test_scratch_end(temporary);
    }
    return result;
}

// The member specifier read (c_parse_member_specifiers_type, #3102) on chosen
// units, in every layout: members read from the tree (primitive words,
// typedef names, qualified ones, a declarator list, a union, and the members
// of a definition nested in a member), and one shape per
// CAstSpecifierFallback a member reaches -- a nested tag and a tag reference
// (lists that are not word-only), `_Atomic` among the words, a word the tree
// calls a type word where the scan finds none, and a restrict-qualified
// typedef under a qualifier. Each analysis with the tree must equal the
// analysis without it, model, bindings and diagnostics alike, and verify mode
// must find every read equal to the machine's scalar frame. The last case is
// the mutation check: a tree that calls a typedef member's `const`
// `volatile` must make verify mode report the difference. (A primitive
// member's row comes from its tokens, which no word tamper changes.)
typedef enum CAstMemberTamper
{
    C_AST_MEMBER_TAMPER_NONE,
    // Call the first SPECIFIER_WORD `int`.
    C_AST_MEMBER_TAMPER_INT,
    // Call the first `const` SPECIFIER_WORD `volatile`.
    C_AST_MEMBER_TAMPER_VOLATILE,
} CAstMemberTamper;

typedef struct CAstMemberCase CAstMemberCase;
struct CAstMemberCase
{
    String8 source;
    CAstMemberTamper tamper;
    // The reason of the member runs handed to the scalar frame, or NONE.
    CAstSpecifierFallback reason;
    u32 fallbacks;
    u32 primitive_answers;
    u32 typedef_answers;
    // Reads verify mode finds differing from the machine: only a tampered
    // tree has any.
    u32 mismatches;
    // The machine diagnoses the unit (with the tree and without it alike).
    bool diagnosed;
};

BUSTER_GLOBAL_LOCAL CAstMemberCase const c_ast_member_cases[] = {
    {S8_INITIALIZER("typedef unsigned long U;\n"
                    "struct A\n"
                    "{\n"
                    "    int a;\n"
                    "    const U b;\n"
                    "    unsigned char c, *d, e[4];\n"
                    "    U volatile f;\n"
                    "    long long g : 3;\n"
                    "};\n"
                    "union B { double x; U y; };\n"
                    "unsigned long f(struct A* p, union B* q)\n"
                    "{\n"
                    "    return p->b + p->f + (U)q->x + q->y + sizeof(struct A);\n"
                    "}\n"),
     C_AST_MEMBER_TAMPER_NONE, C_AST_SPECIFIER_FALLBACK_NONE, 0, 4, 3, 0, false},
    {S8_INITIALIZER("typedef int T;\n"
                    "struct Outer\n"
                    "{\n"
                    "    struct Inner { short s; T t; } inner;\n"
                    "    struct Outer* next;\n"
                    "};\n"),
     C_AST_MEMBER_TAMPER_NONE, C_AST_SPECIFIER_FALLBACK_INPUT, 2, 1, 1, 0, false},
    {S8_INITIALIZER("struct S { _Atomic int a; };\n"), C_AST_MEMBER_TAMPER_NONE, C_AST_SPECIFIER_FALLBACK_SPECIFIERS, 1, 0, 0, 0, false},
    {S8_INITIALIZER("struct S { const x; };\n"), C_AST_MEMBER_TAMPER_INT, C_AST_SPECIFIER_FALLBACK_PRIMITIVE, 1, 0, 0, 0, true},
    {S8_INITIALIZER("typedef int *restrict RP;\nstruct S { const RP p; };\n"), C_AST_MEMBER_TAMPER_NONE, C_AST_SPECIFIER_FALLBACK_TYPEDEF, 1, 0, 0,
     0, false},
    {S8_INITIALIZER("typedef int T;\nstruct S { const T a; };\n"), C_AST_MEMBER_TAMPER_VOLATILE, C_AST_SPECIFIER_FALLBACK_NONE, 0, 0, 1, 1, false},
};

BUSTER_GLOBAL_LOCAL UnitTestResult c_ast_test_members(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(c_ast_member_cases); index += 1)
    {
        CAstMemberCase const* member_case = &c_ast_member_cases[index];
        for (u32 layout = 0; layout < BUSTER_ARRAY_LENGTH(c_ast_test_layouts); layout += 1)
        {
            TemporalArena temporary = scratch_begin(&arguments->arena, 1);
            String8 label = string_format(temporary.arena, S8("member case {u32}, layout {u32}"), index, layout);
            CPreprocessResult preprocess = c_ast_test_preprocess(temporary.arena, member_case->source, C_PREPROCESS_DIALECT_GNU17);
            CAstResult built = c_ast_build(temporary.arena, preprocess, (CAstOptions){.layout = c_ast_test_layouts[layout]});
            if (BUSTER_REQUIRE(arguments, preprocess.error_count == 0 && built.complete))
            {
                CAst tree = c_ast_test_copy(temporary.arena, &built.ast);
                u32 word = C_AST_NODE_INVALID;
                for (u32 node = 0; node < tree.node_count && word == C_AST_NODE_INVALID; node += 1)
                {
                    bool wanted = tree.kinds[node] == C_AST_SPECIFIER_WORD &&
                                  (member_case->tamper != C_AST_MEMBER_TAMPER_VOLATILE || tree.data[node] == C_AST_WORD_CONST);
                    word = wanted ? node : word;
                }
                if (member_case->tamper != C_AST_MEMBER_TAMPER_NONE && BUSTER_REQUIRE(arguments, word != C_AST_NODE_INVALID))
                {
                    tree.data[word] = member_case->tamper == C_AST_MEMBER_TAMPER_INT ? C_AST_WORD_INT : C_AST_WORD_VOLATILE;
                }
                CAnalysisResult plain = c_analyze_semantics_only(temporary.arena, preprocess, c_parse_ast_reference(temporary.arena, preprocess));
                CAstSpecifierStatistics statistics = {0};
                CParserResult syntax = c_parse_ast_reference(temporary.arena, preprocess);
                syntax.ast = &tree;
                syntax.ast_specifier_statistics = &statistics;
                CAnalysisResult read = c_analyze_semantics_only(temporary.arena, preprocess, syntax);
                BUSTER_TEST_RAW(arguments, (plain.diagnostic_count != 0) == member_case->diagnosed, label);
                // A tampered read is wrong by construction; verify mode below
                // must say so, and the model need not match.
                if (!member_case->mismatches)
                {
                    String8 difference = c_ast_corpus_analyses_differ(temporary.arena, &plain, &read);
                    difference = difference.length ? difference : c_ast_corpus_model_differ(temporary.arena, &plain, &read);
                    difference = difference.length ? difference : c_ast_corpus_bindings_differ(temporary.arena, &plain, &read);
                    BUSTER_TEST_RAW(arguments, difference.length == 0, string_format(temporary.arena, S8("{S8}: {S8}"), label, difference));
                }
                bool handed = member_case->reason != C_AST_SPECIFIER_FALLBACK_NONE;
                BUSTER_TEST_RAW(arguments,
                                statistics.member_primitive_answers == member_case->primitive_answers &&
                                    statistics.member_typedef_answers == member_case->typedef_answers &&
                                    statistics.member_fallbacks == member_case->fallbacks &&
                                    (!handed || statistics.member_fallback_counts[member_case->reason] == member_case->fallbacks),
                                string_format(temporary.arena, S8("{S8}: {u64} primitive and {u64} typedef member answers, {u64} fallbacks"), label,
                                              statistics.member_primitive_answers, statistics.member_typedef_answers, statistics.member_fallbacks));
                CAstSpecifierStatistics verified_statistics = {0};
                CParserResult verified = c_parse_ast_reference(temporary.arena, preprocess);
                verified.ast = &tree;
                verified.ast_specifier_statistics = &verified_statistics;
                c_test_ast_members_verify_take();
                c_test_ast_members_verify_set(true);
                CAnalysisResult checked = c_analyze_semantics_only(temporary.arena, preprocess, verified);
                c_test_ast_members_verify_set(false);
                CTestAstMemberVerify verify = c_test_ast_members_verify_take();
                u64 answers = verified_statistics.member_primitive_answers + verified_statistics.member_typedef_answers;
                BUSTER_TEST_RAW(arguments, verify.compared == answers && verify.mismatches == member_case->mismatches,
                                string_format(temporary.arena, S8("{S8}: verify compared {u64} of {u64} reads, {u64} differ"), label, verify.compared,
                                              answers, verify.mismatches));
                // The machine's read, rolled back, leaves the analysis as the
                // read alone leaves it.
                String8 difference = c_ast_corpus_analyses_differ(temporary.arena, &read, &checked);
                difference = difference.length ? difference : c_ast_corpus_model_differ(temporary.arena, &read, &checked);
                BUSTER_TEST_RAW(arguments, difference.length == 0, string_format(temporary.arena, S8("{S8}: verify mode: {S8}"), label, difference));
            }
            c_ast_release(&built.ast);
            c_test_scratch_end(temporary);
        }
    }
    return result;
}

// Token spans (c_ast_spans_build, GitHub #3102 step 1): an operator's span
// reaches over the parentheses that wrap its operands, a call and a subscript
// end at their closer, and c_ast_spans_locate finds a node from its span with
// or without the parentheses that wrap it, but not from a range that is no
// node's span.
BUSTER_GLOBAL_LOCAL UnitTestResult c_ast_test_spans(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    TemporalArena temporary = scratch_begin(&arguments->arena, 1);
    CPreprocessResult preprocess = c_ast_test_preprocess(temporary.arena,
                                                          S8("int f(int, int); int g[4];\n"
                                                             "void h(int c, int d) { (void)((c + d) * f(c, d) + g[2]--); }\n"),
                                                          C_PREPROCESS_DIALECT_GNU17);
    CAstResult built = c_ast_build(temporary.arena, preprocess, (CAstOptions){0});
    u32* matches = c_ast_test_matches(temporary.arena, preprocess);
    CAstSpans spans = {0};
    if (BUSTER_REQUIRE(arguments, built.complete && matches) &&
        BUSTER_REQUIRE(arguments, c_ast_spans_build(temporary.arena, &built.ast, preprocess.tokens, (u32)preprocess.token_count, matches, &spans)))
    {
        CAst const* ast = &built.ast;
        BUSTER_TEST(arguments, c_ast_test_spans_consistent(ast, &spans, preprocess, matches));
        BUSTER_TEST(arguments, spans.retained_bytes == (u64)ast->node_count * 12 + (preprocess.token_count + 1) * 4);
        u32 multiply = c_ast_test_find_kind(ast, C_AST_MULTIPLY);
        u32 decrement = c_ast_test_find_kind(ast, C_AST_POST_DECREMENT);
        u32 cast = c_ast_test_find_kind(ast, C_AST_CAST);
        u32 add = c_ast_test_find_kind(ast, C_AST_ADD);
        if (BUSTER_REQUIRE(arguments, multiply != C_AST_NODE_INVALID && decrement != C_AST_NODE_INVALID && cast != C_AST_NODE_INVALID &&
                                          add != C_AST_NODE_INVALID))
        {
            BUSTER_STRING_TEST(arguments, c_ast_test_span_text(temporary.arena, preprocess, spans.first[multiply], spans.end[multiply]),
                               S8("( c + d ) * f ( c , d )"));
            BUSTER_STRING_TEST(arguments, c_ast_test_span_text(temporary.arena, preprocess, spans.first[decrement], spans.end[decrement]),
                               S8("g [ 2 ] --"));
            BUSTER_STRING_TEST(arguments, c_ast_test_span_text(temporary.arena, preprocess, spans.first[cast], spans.end[cast]),
                               S8("( void ) ( ( c + d ) * f ( c , d ) + g [ 2 ] -- )"));
            // The inner `c + d`, found from its own tokens and from its
            // parenthesized form.
            BUSTER_STRING_TEST(arguments, c_ast_test_span_text(temporary.arena, preprocess, spans.first[add], spans.end[add]), S8("c + d"));
            BUSTER_TEST(arguments, c_ast_spans_locate(&spans, preprocess.tokens, matches, spans.first[add], spans.end[add]) == add);
            BUSTER_TEST(arguments, c_ast_spans_locate(&spans, preprocess.tokens, matches, spans.first[add] - 1, spans.end[add] + 1) == add);
            // `d ) * f` is no node's span.
            BUSTER_TEST(arguments, c_ast_spans_locate(&spans, preprocess.tokens, matches, spans.end[add] - 1, spans.end[add] + 3) ==
                                       C_AST_NODE_INVALID);
            BUSTER_TEST(arguments, spans.first[ast->root] == C_AST_SPAN_NONE);
        }
    }
    // Type names keep the producer's complete boundary; optional, nested and
    // variably modified declarators are checked against literal spellings.
    String8 type_source = S8("int f(int n) { return sizeof(int) + sizeof(int (*)[n]) + sizeof(__typeof__(__typeof__(int)) *) + sizeof(int (*)(void)); }");
    String8 const type_names[] = {S8("int"), S8("int ( * ) [ n ]"), S8("int"), S8("__typeof__ ( int )"),
                                 S8("__typeof__ ( __typeof__ ( int ) ) *"), S8("int ( * ) ( void )")};
    CPreprocessResult type_preprocess = c_ast_test_preprocess(temporary.arena, type_source, C_PREPROCESS_DIALECT_GNU17);
    for (u32 batch = 0; batch < 8; batch += 1)
    {
        CAstResult type_tree = c_ast_build(temporary.arena, type_preprocess, (CAstOptions){.refill_batch = batch});
        u32* type_matches = c_ast_test_matches(temporary.arena, type_preprocess);
        CAstSpans type_spans = {0};
        bool ready = BUSTER_REQUIRE(arguments, type_tree.complete && c_ast_validate(&type_tree.ast) == C_AST_NODE_INVALID) &&
                     BUSTER_REQUIRE(arguments, c_ast_spans_build(temporary.arena, &type_tree.ast, type_preprocess.tokens,
                                                                    (u32)type_preprocess.token_count, type_matches, &type_spans));
        if (ready)
        {
            u32 seen = 0;
            for (u32 node = 0; node < type_tree.ast.node_count; node += 1)
            {
                if (type_tree.ast.kinds[node] == C_AST_TYPE_NAME)
                {
                    BUSTER_TEST(arguments, c_ast_child_count(&type_tree.ast, node) >= 1 && c_ast_child_count(&type_tree.ast, node) <= 2);
                    BUSTER_TEST(arguments, type_spans.first[node] == type_tree.ast.tokens[node] && type_spans.end[node] == type_tree.ast.data[node]);
                    if (BUSTER_REQUIRE(arguments, seen < BUSTER_ARRAY_LENGTH(type_names)))
                    {
                        BUSTER_STRING_TEST(arguments, c_ast_test_span_text(temporary.arena, type_preprocess, type_spans.first[node], type_spans.end[node]),
                                           type_names[seen]);
                    }
                    seen += 1;
                }
            }
            BUSTER_TEST(arguments, seen == BUSTER_ARRAY_LENGTH(type_names));
        }
        c_ast_release(&type_tree.ast);
    }
    // Without a delimiter index there are no spans.
    CAstSpans none = {.spanned = 1};
    BUSTER_TEST(arguments, !c_ast_spans_build(temporary.arena, &built.ast, preprocess.tokens, (u32)preprocess.token_count, 0, &none) &&
                               none.spanned == 0 && !none.first);
    c_ast_release(&built.ast);
    c_test_scratch_end(temporary);
    return result;
}

// The initializer validator publishes its selections, including paths that
// brace elision and anonymous-member continuations make implicit in syntax.
BUSTER_GLOBAL_LOCAL UnitTestResult c_ast_test_initializer_plans(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    TemporalArena temporary = scratch_begin(&arguments->arena, 1);
    Arena* arena = temporary.arena;
    String8 source = S8("struct S { int : 0; int x; struct { int y, z; }; char text[4]; };\n"
                        "struct S object = { .y = 4, 5, .text = {\"abc\"}, .x = 2 };\n"
                        "int table[] = { [2 ... 4] = 1, [3] = 9, 7 };\n"
                        "int matrix[2][2] = { 1, 2, 3, 4 };\n"
                        "char* names[] = { \"argument\" }; char letters[] = { \"abc\" };\n"
                        "struct T { char text[4]; }; struct T sole = { \"abc\" }; struct T sole_array[] = { \"abc\" };\n"
                        "double _Complex complex_value = { 1.0, 2.0 };\n"
                        "struct F { int head; char tail[]; }; struct F flex = { 9, { -1, -1, -1 } };\n"
                        "struct F text_flex = { 7, \"abc\" };\n"
                        "struct P { int x, y; }; struct P bare_range[4] = { [1 ... 3] = 1, 2 };\n"
                        "struct P braced_range[4] = { [1 ... 3] = { 1 }, [3].y = 2 };\n"
                        "typedef unsigned int V __attribute__((vector_size(32))); union U { V v; unsigned int lanes[8]; };\n"
                        "unsigned int g(unsigned int n, ...) { __builtin_va_list ap; __builtin_va_start(ap, n);\n"
                        "union U u = { .v = __builtin_va_arg(ap, V) }; __builtin_va_end(ap); return u.lanes[0]; }\n"
                        "int f(void) { static int selected = ({ 41; }); struct S local = { .y = 1, 2 }; int conditional[] = { 1 ? 2, 3 : 4, 5 };\n"
                        "return ((int[]){ 3, 4 })[0] + local.z + conditional[1]; }\n");
    CPreprocessResult preprocess = c_ast_test_preprocess(arena, source, C_PREPROCESS_DIALECT_GNU17);
    CAstResult built = c_ast_build(arena, preprocess, (CAstOptions){0});
    CAstFacts facts = {0};
    bool ready = BUSTER_REQUIRE(arguments, preprocess.error_count == 0 && built.complete) &&
                 BUSTER_REQUIRE(arguments, c_ast_facts_create(&facts, &built.ast));
    if (ready)
    {
        CParserResult syntax = c_parse_ast_reference(arena, preprocess);
        syntax.ast = &built.ast;
        syntax.ast_facts = &facts;
        syntax.type_interning = true;
        CAnalysisResult analysis = c_analyze_semantics_only(arena, preprocess, syntax);
        BUSTER_TEST(arguments, analysis.analysis_complete && analysis.diagnostic_count == 0);
        u32 plans = 0;
        u32 values = 0;
        u32 ranged = 0;
        u32 promoted = 0;
        u32 nested = 0;
        u32 flexible = 0;
        u32 explicit_paths = 0;
        u32 implicit_paths = 0;
        u32 refused_ranges = 0;
        u32 selected_tails = 0;
        for (CInitializerPlan const* plan = analysis.initializer_plans; plan; plan = plan->next)
        {
            BUSTER_TEST(arguments, plan->complete && !plan->failed && plan->node < built.ast.node_count);
            if (built.ast.kinds[plan->node] == C_AST_STATEMENT_EXPRESSION)
            {
                selected_tails += 1;
                BUSTER_TEST(arguments, plan->item_count == 1 && built.ast.kinds[plan->items[0].node] == C_AST_NUMBER &&
                    string_equal(c_token_spelling(preprocess.spelling_base, preprocess.tokens[plan->items[0].value_start]), S8("41")));
            }
            plans += plan->complete;
            if (plan->flags & C_INITIALIZER_PLAN_BARE_AGGREGATE_RANGE)
            {
                refused_ranges += 1;
                BUSTER_TEST(arguments, plan->unsupported_range_token_plus_one &&
                    preprocess.tokens[plan->unsupported_range_token_plus_one - 1].kind == C_TOKEN_PREPROCESSING_NUMBER);
            }
            if (plan->flexible_count)
            {
                flexible += 1;
                BUSTER_TEST(arguments, plan->flexible_member_plus_one == 2 &&
                    (plan->flexible_count == 3 || plan->flexible_count == 4));
                CType tail = analysis.types[analysis.members[analysis.types[plan->type.value].member_start + 1].type.value];
                BUSTER_TEST(arguments, !analysis.array_bounds[tail.array_bound].has_inferred_count);
            }
            if (plan->type.value < analysis.type_count && analysis.types[plan->type.value].kind == C_TYPE_ARRAY)
            {
                CType array = analysis.types[plan->type.value];
                if (array.element_type.value < analysis.type_count && analysis.types[array.element_type.value].kind == C_TYPE_POINTER)
                    BUSTER_TEST(arguments, plan->item_count == 3 && plan->items[1].path_plus_one != 0 && plan->inferred_count == 1);
            }
            for (u32 path = 0; path < plan->path_count; path += 1)
            {
                CInitializerPath edge = plan->paths[path];
                BUSTER_TEST(arguments, edge.container.value < analysis.type_count && edge.parent_plus_one <= plan->path_count);
                ranged += edge.first != edge.last;
                explicit_paths += (edge.flags & C_INITIALIZER_PATH_EXPLICIT) != 0;
                implicit_paths += (edge.flags & C_INITIALIZER_PATH_EXPLICIT) == 0;
                if (edge.first != edge.last) BUSTER_TEST(arguments, (edge.flags & C_INITIALIZER_PATH_EXPLICIT) != 0);
                if (edge.member != UINT32_MAX && edge.container.value < analysis.type_count)
                {
                    CType container = analysis.types[edge.container.value];
                    BUSTER_TEST(arguments, c_type_kind_is_complex(container.kind) ? edge.member < 2 : edge.member < container.member_count);
                    promoted += edge.parent_plus_one != 0;
                }
                else nested += edge.parent_plus_one != 0;
            }
            for (u32 item = 0; item < plan->item_count; item += 1)
            {
                CInitializerItem value = plan->items[item];
                BUSTER_TEST(arguments, value.node < built.ast.node_count && value.type.value < analysis.type_count &&
                                       value.path_plus_one <= plan->path_count);
                values += (value.flags & C_INITIALIZER_ITEM_VALUE) != 0;
                if ((value.flags & C_INITIALIZER_ITEM_VALUE) && value.type.value < analysis.type_count && analysis.types[value.type.value].kind == C_TYPE_VECTOR)
                    BUSTER_TEST(arguments, value.path_plus_one && plan->paths[value.path_plus_one - 1].member != UINT32_MAX);
                if (value.flags & (C_INITIALIZER_ITEM_LIST_BEGIN | C_INITIALIZER_ITEM_LIST_END))
                    BUSTER_TEST(arguments, value.paired_item_plus_one != 0 && value.paired_item_plus_one <= plan->item_count &&
                        plan->items[value.paired_item_plus_one - 1].node == value.node);
            }
            if (plan->type.value < analysis.type_count && analysis.types[plan->type.value].kind == C_TYPE_ARRAY &&
                plan->inferred_count == 5)
                BUSTER_TEST(arguments, plan->item_count >= 3 && plan->path_count >= 3);
        }
        BUSTER_TEST(arguments, plans >= 5 && values >= 14 && ranged > 0 && promoted > 0 && nested > 0);
        BUSTER_TEST(arguments, flexible == 2);
        BUSTER_TEST(arguments, explicit_paths && implicit_paths);
        BUSTER_TEST(arguments, refused_ranges == 1 && selected_tails == 1);
    }
    c_ast_facts_release(&facts);
    c_ast_release(&built.ast);
    c_test_scratch_end(temporary);
    return result;
}

// Inferred counts belong to semantic output. Lowering privately completes an
// omitted bound that still needs F5's plan, and a second lower sees the same unit.
BUSTER_GLOBAL_LOCAL UnitTestResult c_ast_test_initializer_lower_reuse(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    TemporalArena temporary = scratch_begin(&arguments->arena, 1);
    CPreprocessResult preprocess = c_ast_test_preprocess(temporary.arena,
        S8("static char *names[] = { \"argument\" }; char text[] = { \"abc\" };"
           "struct P { int x, y; }; struct P rows[] = { [1] = { 3, 4 } };"
           "struct T { char text[4]; }; struct T sole = { \"abc\" }; struct T sole_array[] = { \"abc\" };"
           "int f(void) { static int local[] = { 5, 6 }; return local[1] + rows[1].y + text[0] + names[0][0]; }"),
        C_PREPROCESS_DIALECT_GNU17);
    CAnalysisResult analysis = c_parse_for_lowering(temporary.arena, preprocess);
    if (BUSTER_REQUIRE(arguments, analysis.analysis_complete && !analysis.diagnostic_count))
    {
        u64 bound_bytes = sizeof(CArrayBound) * (u64)analysis.array_bound_count;
        CArrayBound* before = arena_allocate(temporary.arena, CArrayBound, analysis.array_bound_count);
        memcpy(before, analysis.array_bounds, bound_bytes);
        CIRLowerResult first = c_lower_to_ir_with_options(temporary.arena, S8("initializer-reuse.c"), preprocess, analysis, target_native, (CIRLowerOptions){0});
        BUSTER_TEST(arguments, !memcmp(before, analysis.array_bounds, bound_bytes));
        CIRLowerResult second = c_lower_to_ir_with_options(temporary.arena, S8("initializer-reuse.c"), preprocess, analysis, target_native, (CIRLowerOptions){0});
        BUSTER_TEST(arguments, !memcmp(before, analysis.array_bounds, bound_bytes));
        if (BUSTER_REQUIRE(arguments, first.program && second.program && !first.diagnostic_count && !second.diagnostic_count &&
                                     first.canonical_ir_certified && second.canonical_ir_certified))
        {
            BUSTER_TEST(arguments, first.program->types.count == second.program->types.count && first.program->symbols.count == second.program->symbols.count);
            IrModule* a = first.program->modules;
            IrModule* b = second.program->modules;
            BUSTER_TEST(arguments, a->global_count == b->global_count);
            for (u32 index = 0; index < a->global_count && index < b->global_count; index += 1)
            {
                IrGlobal* x = a->globals + index;
                IrGlobal* y = b->globals + index;
                IrSymbol* symbol = ir_symbol_from_id(&first.program->symbols, x->symbol);
                if (symbol && (string_equal(symbol->name, S8("sole")) || string_equal(symbol->name, S8("sole_array"))))
                    BUSTER_TEST(arguments, x->bytes.pointer && x->bytes.length == 4 && !memcmp(x->bytes.pointer, "abc", 4));
                BUSTER_TEST(arguments, x->initializer_kind == y->initializer_kind && x->initializer_bits == y->initializer_bits &&
                    x->bytes.length == y->bytes.length && x->relocation_count == y->relocation_count && x->type.value == y->type.value);
                if (x->bytes.length == y->bytes.length && x->bytes.length)
                    BUSTER_TEST(arguments, !memcmp(x->bytes.pointer, y->bytes.pointer, x->bytes.length));
                for (u32 relocation = 0; relocation < x->relocation_count && relocation < y->relocation_count; relocation += 1)
                    BUSTER_TEST(arguments, x->relocations[relocation].offset == y->relocations[relocation].offset &&
                        x->relocations[relocation].symbol.value == y->relocations[relocation].symbol.value &&
                        x->relocations[relocation].addend == y->relocations[relocation].addend);
            }
        }
    }
    c_test_scratch_end(temporary);
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult c_ast_test_initializer_promoted_depth(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    TemporalArena temporary = scratch_begin(&arguments->arena, 1);
    u32 const depth = 128;
    String8 body = S8("struct { int x, y; };");
    for (u32 level = 0; level < depth; level += 1)
        body = string_format(temporary.arena, S8("union {{ {S8} }};"), body);
    String8 source = string_format(temporary.arena,
        S8("struct D {{ {S8} }}; struct D designated = {{.x=7}}; struct D flat = {{1,2}}; struct D inferred[] = {{[0].x=9}};"), body);
    CPreprocessResult preprocess = c_ast_test_preprocess(temporary.arena, source, C_PREPROCESS_DIALECT_GNU17);
    CAnalysisResult analysis = c_parse_for_lowering(temporary.arena, preprocess);
    u32 plans = 0;
    u32 inferred = 0;
    u32 bound_index = UINT32_MAX;
    for (CInitializerPlan const* plan = analysis.initializer_plans; plan; plan = plan->next)
    {
        plans += 1;
        BUSTER_TEST(arguments, plan->complete && !plan->failed && plan->path_count > depth);
        CType type = analysis.types[plan->type.value];
        if (type.kind == C_TYPE_ARRAY)
        {
            inferred += 1;
            bound_index = type.array_bound;
            BUSTER_TEST(arguments, plan->inferred_count == 1 && bound_index < analysis.array_bound_count);
            if (bound_index < analysis.array_bound_count)
                BUSTER_TEST(arguments, !analysis.array_bounds[bound_index].has_inferred_count);
        }
    }
    BUSTER_TEST(arguments, analysis.analysis_complete && !analysis.diagnostic_count && plans == 3 && inferred == 1);
    CIRLowerResult lowered = c_lower_to_ir(temporary.arena, S8("deep-initializer-plan.c"), preprocess, analysis, target_native);
    if (BUSTER_REQUIRE(arguments, lowered.program && !lowered.diagnostic_count && lowered.canonical_ir_certified))
    {
        struct { String8 name; u32 values[2]; } const expected[] = {
            {S8("designated"), {7, 0}}, {S8("flat"), {1, 2}}, {S8("inferred"), {9, 0}},
        };
        IrModule* module = lowered.program->modules;
        u32 checked = 0;
        for (u32 index = 0; index < module->global_count; index += 1)
        {
            IrGlobal* global = module->globals + index;
            IrSymbol* symbol = ir_symbol_from_id(&lowered.program->symbols, global->symbol);
            for (u32 row = 0; symbol && row < BUSTER_ARRAY_LENGTH(expected); row += 1)
            {
                if (string_equal(symbol->name, expected[row].name))
                {
                    checked += 1;
                    BUSTER_TEST(arguments, global->initializer_kind == IR_GLOBAL_INITIALIZER_BYTES && global->bytes.length == sizeof(expected[row].values));
                    if (global->bytes.length == sizeof(expected[row].values))
                        BUSTER_TEST(arguments, memcmp(global->bytes.pointer, expected[row].values, sizeof(expected[row].values)) == 0);
                }
            }
        }
        BUSTER_TEST(arguments, checked == BUSTER_ARRAY_LENGTH(expected));
        if (bound_index < analysis.array_bound_count)
            BUSTER_TEST(arguments, !analysis.array_bounds[bound_index].has_inferred_count);
    }
    c_test_scratch_end(temporary);
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult c_ast_test_static_plan_object_contract(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    TemporalArena temporary = scratch_begin(&arguments->arena, 1);
    CPreprocessResult preprocess = c_ast_test_preprocess(temporary.arena,
        S8("int target_a, target_b; struct R { int *x, *y; };"
           "static struct R *q = &(struct R){ .x = &target_a, .y = &target_b, .x = 0 };"
           "static struct R *range[3] = { [0 ... 2] = &(struct R){ .x = &target_a, .y = &target_b, .x = 0 } };"
           "static _Complex double double_braced = {{3}};"),
        C_PREPROCESS_DIALECT_GNU17);
    CAnalysisResult analysis = c_parse_for_lowering(temporary.arena, preprocess);
    CIRLowerResult lowered = c_lower_to_ir(temporary.arena, S8("compound-relocation-contract.c"), preprocess, analysis, target_native);
    if (BUSTER_REQUIRE(arguments, lowered.program && !analysis.diagnostic_count && !lowered.diagnostic_count && lowered.canonical_ir_certified))
    {
        IrModule* module = lowered.program->modules;
        u32 objects = 0;
        u32 ranged = 0;
        u32 complex = 0;
        u64 pointer_size = lowered.program->data_layout.pointer.size;
        for (u32 index = 0; index < module->global_count; index += 1)
        {
            IrGlobal* global = module->globals + index;
            IrSymbol* symbol = ir_symbol_from_id(&lowered.program->symbols, global->symbol);
            if (symbol && string_starts_with_sequence(symbol->name, S8(".L.compoundliteral.")))
            {
                objects += 1;
                BUSTER_TEST(arguments, global->initializer_kind == IR_GLOBAL_INITIALIZER_BYTES && global->bytes.length == pointer_size * 2 &&
                    global->relocation_count == 1);
                if (global->relocation_count == 1)
                {
                    IrSymbol* target = ir_symbol_from_id(&lowered.program->symbols, global->relocations[0].symbol);
                    BUSTER_TEST(arguments, target && string_equal(target->name, S8("target_b")) && global->relocations[0].offset == pointer_size);
                }
            }
            if (symbol && string_equal(symbol->name, S8("range")))
            {
                ranged += 1;
                BUSTER_TEST(arguments, global->relocation_count == 3);
                for (u32 row = 0; row < global->relocation_count; row += 1)
                    BUSTER_TEST(arguments, global->relocations[row].offset == (u64)row * pointer_size &&
                        global->relocations[row].symbol.value == global->relocations[0].symbol.value);
            }
            if (symbol && string_equal(symbol->name, S8("double_braced")))
            {
                complex += 1;
                f64 real = 3.0;
                BUSTER_TEST(arguments, global->initializer_kind == IR_GLOBAL_INITIALIZER_BYTES && global->bytes.length == sizeof(real) * 2);
                if (global->bytes.length == sizeof(real) * 2)
                {
                    BUSTER_TEST(arguments, memcmp(global->bytes.pointer, &real, sizeof(real)) == 0);
                    for (u32 byte = sizeof(real); byte < sizeof(real) * 2; byte += 1)
                        BUSTER_TEST(arguments, !global->bytes.pointer[byte]);
                }
            }
        }
        BUSTER_TEST(arguments, objects == 2 && ranged == 1 && complex == 1);
    }
    c_test_scratch_end(temporary);
    struct { String8 source, message; u32 column; } const refusals[] = {
        {S8("struct S { unsigned a:4, b:4; }; static struct S s = { .a=7, .b={} };"), S8("unsupported C global initializer for 's'")},
        {S8("struct S { unsigned a:4, b:4; }; static struct S s = { .a=7, .b={3} };"), S8("unsupported C global initializer for 's'")},
        {S8("struct S { int a, b; }; static struct S s = { .a=7, .b={} };"), S8("C IR lowering: cannot fold '{' in a static initializer")},
        {S8("static int a[2] = { [0 ... 1] = {3} };"), S8("C IR lowering: initializer has more elements than the aggregate can hold")},
        {S8("static int *p = &(int){};"), S8("C IR lowering: cannot fold '{' in a static initializer")},
        {S8("static int a = {{3}};"), S8("C IR lowering: too many braces around scalar initializer"), 17},
        {S8("static int a = {{{}}};"), S8("C IR lowering: too many braces around scalar initializer"), 17},
        {S8("static int a = (int){{3}};"), S8("C IR lowering: too many braces around scalar initializer"), 22},
        {S8("struct S { int a; }; static struct S s = { .a={{3}} };"), S8("C IR lowering: cannot fold '{' in a static initializer"), 47},
        {S8("struct S { int a; }; static struct S s = { .a={} };"), S8("C IR lowering: cannot fold '{' in a static initializer"), 47},
        {S8("static int *p = &(int){{3}};"), S8("C IR lowering: cannot fold '{' in a static initializer"), 23},
        {S8("static int *p = &(int){{}};"), S8("C IR lowering: cannot fold '{' in a static initializer"), 23},
        {S8("int f(void) { static int x = {{3}}; return x; }"), S8("in function 'f': could not lower static initializer for local 'x'"), 5},
        {S8("int f(void) { static int x = (int){{}}; return x; }"), S8("in function 'f': could not lower static initializer for local 'x'"), 5},
        {S8("static _Complex double z = {{}};"), S8("C IR lowering: cannot fold '{' in a static initializer"), 29},
        {S8("static _Complex double z = {{{3}}};"), S8("C IR lowering: cannot fold '{' in a static initializer"), 29},
    };
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(refusals); index += 1)
    {
        temporary = scratch_begin(&arguments->arena, 1);
        preprocess = c_ast_test_preprocess(temporary.arena, refusals[index].source, C_PREPROCESS_DIALECT_GNU17);
        analysis = c_parse_for_lowering(temporary.arena, preprocess);
        lowered = c_lower_to_ir(temporary.arena, S8("static-brace-contract.c"), preprocess, analysis, target_native);
        BUSTER_TEST(arguments, !analysis.diagnostic_count && !lowered.canonical_ir_certified && lowered.diagnostic_count == 1);
        if (lowered.diagnostic_count == 1) BUSTER_STRING_TEST(arguments, lowered.diagnostics[0].message, refusals[index].message);
        if (lowered.diagnostic_count == 1 && refusals[index].column)
            BUSTER_TEST(arguments, lowered.diagnostics[0].location.line == 1 && lowered.diagnostics[0].location.column == refusals[index].column);
        c_test_scratch_end(temporary);
    }
    return result;
}

// The facts semantic analysis retains for lowering (CAstFacts, GitHub #4062).
// One function body and two file-scope initializers are analyzed with the
// tree and a facts record, and once with the tree alone; the model must be
// the same, since publication keeps nothing the machine appended. The
// columns then hold, among others: a bit-field member's width, an
// identifier's entity, a string literal in structural form (its element row
// and its length with the terminator), the type of a `[]` over a string
// literal whose own run appended the literal's row, and the literals of an
// initializer no query typed. `__func__` and a builtin's callee name are
// gaps: the machine types them to nothing. Every candidate is accounted for
// once, and no published row is past the type table.
BUSTER_GLOBAL_LOCAL UnitTestResult c_ast_test_constant_node_tasks(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    TemporalArena source_temporary = scratch_begin(&arguments->arena, 1);
    u32 depth = 4096;
    char8* deep = arena_allocate(source_temporary.arena, char8, depth * 3 + 1);
    for (u32 index = 0; index < depth; index += 1) { deep[index * 2] = '+'; deep[index * 2 + 1] = '('; }
    deep[depth * 2] = '1';
    memset(deep + depth * 2 + 1, ')', depth);
    struct { String8 expression; u64 integer; f64 floating; bool is_float; } const cases[] = {
        {S8("(1 + 2) * (3 + 4)"), 21, 0, false},
        {S8("0 && (1 / 0)"), 0, 0, false},
        {S8("1 ? 7 : (1 / 0)"), 7, 0, false},
        {S8("0 ? (1 / 0) : 9"), 9, 0, false},
        {S8("1.25 + 2.5"), 0, 3.75, true},
        {S8("5.0 + 7.0i"), 5, 0, false},
        {S8("__builtin_complex(1.0, 2.0) + 3.0"), 4, 0, false},
        {S8("__real__ (5.0 + 7.0i)"), 0, 5.0, true},
        {S8("__imag__ (5.0 + 7.0i)"), 0, 7.0, true},
        {S8("__imag__ 5.0"), 0, 0.0, true},
        {S8("_Generic(0, int: 13, default: (1 / 0))"), 13, 0, false},
        {S8("__builtin_constant_p(__builtin_constant_p(input))"), 1, 0, false},
        {S8("__builtin_constant_p(5.0 + 7.0i)"), 1, 0, false},
        {S8("__builtin_choose_expr(__builtin_constant_p(5), 13, (1 / 0))"), 13, 0, false},
        {S8("__builtin_types_compatible_p(int, unsigned int)"), 0, 0, false},
        {S8("__builtin_classify_type(1.0)"), 8, 0, false},
        {S8("__builtin_bswap32(0x12345678)"), 0x78563412, 0, false},
        {S8("__builtin_popcount(0xf0)"), 4, 0, false},
        {S8("__builtin_nanf(\"\") != __builtin_nanf(\"\")"), 1, 0, false},
        {S8("__builtin_huge_val() > 0.0"), 1, 0, false},
        {S8("sizeof(\"ab\" \"cd\") + 1"), 6, 0, false},
        {S8("sizeof(u\"ab\")"), 6, 0, false},
        {S8("__real__ (1.5f16 - 2.0f16i)"), 0, 1.5, true},
        {S8("__imag__ (1.5f16 - 2.0f16i)"), 0, -2.0, true},
        {{.pointer = deep, .length = depth * 3 + 1}, 1, 0, false},
    };
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(cases); index += 1)
    {
        Arena* conflicts[] = {arguments->arena, source_temporary.arena};
        TemporalArena temporary = scratch_begin(conflicts, BUSTER_ARRAY_LENGTH(conflicts));
        String8 source = string_format(temporary.arena, S8("int f(int input) {{ return {S8}; }}"), cases[index].expression);
        CPreprocessResult preprocess = c_ast_test_preprocess(temporary.arena, source, C_PREPROCESS_DIALECT_GNU17);
        CAstResult built = c_ast_build(temporary.arena, preprocess, (CAstOptions){0});
        CAstFacts facts = {0};
        if (BUSTER_REQUIRE(arguments, built.complete && c_ast_facts_create(&facts, &built.ast)))
        {
            CParserResult syntax = c_parse_ast(temporary.arena, preprocess);
            syntax.ast = &built.ast;
            syntax.ast_facts = &facts;
            CAnalysisResult analysis = c_analyze_semantics_only(temporary.arena, preprocess, syntax);
            BUSTER_TEST(arguments, analysis.analysis_complete && !analysis.diagnostic_count);
            u32 expression = C_AST_NODE_INVALID;
            for (u32 node = 0; node < built.ast.node_count; node += 1)
                if (built.ast.kinds[node] == C_AST_RETURN) expression = c_ast_child_at(&built.ast, node, 0);
            u64 low = 0;
            u64 high = 0;
            f64 floating = 0;
            bool is_float = false;
            bool evaluated = c_test_constant_node_numeric(temporary.arena, preprocess, &built.ast, &facts, expression,
                                                          &low, &high, &floating, &is_float);
            BUSTER_TEST_RAW(arguments, evaluated && is_float == cases[index].is_float &&
                                       (is_float ? floating == cases[index].floating : low == cases[index].integer && !high), cases[index].expression);
        }
        c_ast_facts_release(&facts);
        c_ast_release(&built.ast);
        c_test_scratch_end(temporary);
    }
    c_test_scratch_end(source_temporary);
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult c_ast_test_constant_object_context(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    String8 const sources[] = {
        S8("struct Node { int* p; }; static struct Node* q = &(struct Node){ .p = &(int){7} };\n"),
        S8("struct Node { int* p; }; static struct Node* q = 1 ? &(struct Node){ .p = &(int){7} } : 0;\n"),
    };
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(sources); index += 1)
    {
        TemporalArena temporary = scratch_begin(&arguments->arena, 1);
        CPreprocessResult preprocess = c_ast_test_preprocess(temporary.arena, sources[index], C_PREPROCESS_DIALECT_GNU17);
        CParserResult syntax = c_parse_ast(temporary.arena, preprocess);
        CAnalysisResult analysis = c_analyze_semantics_only(temporary.arena, preprocess, syntax);
        BUSTER_TEST(arguments, analysis.analysis_complete && !analysis.diagnostic_count);
        CIRLowerResult lowered = c_lower_to_ir(temporary.arena, S8("compound-context.c"), preprocess, analysis, target_native);
        if (!index) BUSTER_TEST(arguments, lowered.program && !lowered.diagnostic_count && lowered.canonical_ir_certified);
        else
        {
            BUSTER_TEST(arguments, !lowered.canonical_ir_certified && lowered.diagnostic_count);
            if (lowered.diagnostic_count)
                BUSTER_TEST(arguments, string_first_sequence(lowered.diagnostics[0].message, S8("nested compound literal objects")) != BUSTER_STRING_NO_MATCH);
        }
        c_test_scratch_end(temporary);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult c_ast_test_bound_owners(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    String8 source = S8("typedef int A[3]; static int a[sizeof(int) + 1];\n"
        "_Alignas(sizeof(long)) static int aligned; _Alignas(long) static int typed;\n"
        "struct __attribute__((aligned(sizeof(int (*)(int))))) Bits { unsigned x : sizeof(int); int y; };\n"
        "void f(int n, int p[static n + 1], int (*q)[n + 2]) {\n"
        " int v[n + 3]; int (*r)[sizeof(int (*)(int)) + n]; (void)p; (void)q; (void)v; (void)r; }\n");
    for (u32 layout = 0; layout < BUSTER_ARRAY_LENGTH(c_ast_test_layouts); layout += 1)
    {
        TemporalArena temporary = scratch_begin(&arguments->arena, 1);
        CPreprocessResult preprocess = c_ast_test_preprocess(temporary.arena, source, C_PREPROCESS_DIALECT_GNU17);
        CAstResult built = c_ast_build(temporary.arena, preprocess, (CAstOptions){.layout = c_ast_test_layouts[layout]});
        CAstFacts facts = {0};
        if (BUSTER_REQUIRE(arguments, built.complete && c_ast_facts_create(&facts, &built.ast)))
        {
            CParserResult syntax = c_parse_ast(temporary.arena, preprocess);
            syntax.ast = &built.ast;
            CAnalysisResult plain = c_analyze_semantics_only(temporary.arena, preprocess, syntax);
            syntax.ast_facts = &facts;
            CAnalysisResult analysis = c_analyze_semantics_only(temporary.arena, preprocess, syntax);
            BUSTER_TEST(arguments, analysis.analysis_complete && !analysis.diagnostic_count && facts.bound_count == analysis.array_bound_count && facts.bound_arena);
            u64 rows = 0;
            String8 difference = c_ast_corpus_analyses_differ(temporary.arena, &plain, &analysis);
            difference = difference.length ? difference : c_ast_corpus_models_differ(temporary.arena, &plain, &analysis, &rows);
            BUSTER_TEST_RAW(arguments, !difference.length, difference);
            u32 owners = 0;
            for (u32 index = 0; index < analysis.array_bound_count; index += 1)
            {
                CArrayBound bound = analysis.array_bounds[index];
                u32 node = c_ast_facts_bound_node(&facts, index);
                if (bound.token_count && !bound.is_star)
                {
                    BUSTER_TEST(arguments, node < built.ast.node_count && (built.ast.kinds[node] == C_AST_ADD || built.ast.kinds[node] == C_AST_NUMBER));
                    bool first = true;
                    for (u32 previous = 0; previous < index; previous += 1) first &= c_ast_facts_bound_node(&facts, previous) != node;
                    owners += first;
                }
                else BUSTER_TEST(arguments, node == C_AST_NODE_INVALID);
            }
            BUSTER_TEST(arguments, owners == 6 && c_ast_facts_bound_node(&facts, facts.bound_count) == C_AST_NODE_INVALID);
            BUSTER_TEST(arguments, facts.alignment_count == analysis.alignment_count && facts.alignment_count >= 3 && facts.member_width_count == 1);
            u32 standard = 0;
            for (u32 index = 0; index < analysis.alignment_count; index += 1)
            {
                u32 node = c_ast_facts_alignment_node(&facts, index);
                BUSTER_TEST(arguments, node < built.ast.node_count);
                standard += c_ast_facts_alignment_is_standard(&facts, index);
            }
            BUSTER_TEST(arguments, standard == 2 && c_ast_facts_alignment_node(&facts, facts.alignment_count) == C_AST_NODE_INVALID);
            u32 width_member = facts.member_width_count ? facts.member_widths[0].member : UINT32_MAX;
            u32 width_node = c_ast_facts_member_width_node(&facts, width_member);
            BUSTER_TEST(arguments, width_member < analysis.member_count && width_node < built.ast.node_count &&
                                   built.ast.kinds[width_node] == C_AST_SIZEOF_TYPE &&
                                   c_ast_facts_member_width_node(&facts, analysis.member_count) == C_AST_NODE_INVALID);
            u32 count = facts.bound_count;
            u32* snapshot = arena_allocate(temporary.arena, u32, count);
            memcpy(snapshot, facts.bound_nodes, sizeof(u32) * count);
            u32* alignment_snapshot = arena_allocate(temporary.arena, u32, facts.alignment_count);
            memcpy(alignment_snapshot, facts.alignment_nodes, sizeof(u32) * facts.alignment_count);
            u64 standard_bytes = ((u64)facts.alignment_count + 7) / 8;
            u8* standard_snapshot = arena_allocate(temporary.arena, u8, standard_bytes);
            memcpy(standard_snapshot, facts.alignment_standard, standard_bytes);
            CAnalysisResult repeated = c_analyze_semantics_only(temporary.arena, preprocess, syntax);
            difference = c_ast_corpus_models_differ(temporary.arena, &analysis, &repeated, &rows);
            BUSTER_TEST_RAW(arguments, !difference.length, difference);
            BUSTER_TEST(arguments, facts.bound_count == count && !memcmp(snapshot, facts.bound_nodes, sizeof(u32) * count));
            u64 copied = c_ast_facts_retain_in_arena(temporary.arena, &facts);
            BUSTER_TEST(arguments, copied >= sizeof(u32) * count && !facts.bound_arena && facts.bound_count == count &&
                                   !memcmp(snapshot, facts.bound_nodes, sizeof(u32) * count));
            BUSTER_TEST(arguments, !c_ast_facts_prepare_bound_nodes(&facts, count));
            BUSTER_TEST(arguments, !c_ast_facts_prepare_value_owners(&facts, count, facts.alignment_count, facts.member_width_count));
            BUSTER_TEST(arguments, c_ast_facts_member_width_node(&facts, width_member) == width_node && facts.alignment_count == analysis.alignment_count);
            BUSTER_TEST(arguments, !memcmp(alignment_snapshot, facts.alignment_nodes, sizeof(u32) * facts.alignment_count) &&
                                   !memcmp(standard_snapshot, facts.alignment_standard, standard_bytes));
        }
        c_ast_facts_release(&facts);
        BUSTER_TEST(arguments, !facts.bound_nodes && !facts.bound_count && !facts.bound_arena && !facts.alignment_nodes &&
                               !facts.alignment_standard && !facts.alignment_count && !facts.member_widths && !facts.member_width_count);
        c_ast_release(&built.ast);
        c_test_scratch_end(temporary);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult c_ast_test_constant_facts(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    TemporalArena temporary = scratch_begin(&arguments->arena, 1);
    String8 valid_source = S8("enum { A = 1 + 2 * 3, B = 0 && (1 / 0), C = 1 ? 9 : (1 / 0),\n"
           " D = _Generic(0, int: 11, default: (1 / 0)), E = __builtin_choose_expr(0, (1 / 0), 13) };\n"
           "static int values[sizeof(unsigned long) + A];\n"
           "static int object; static int *address = &object;\n"
           "static double real = 1.25 + 2.5;\n"
           "static long double extended = 0x1.8p0L;\n"
           "static long double extended_array[2] = { 0x1.8p0L, { -0x1.8p0L } };\n"
           "static struct Extended { long double real; } extended_record = { .real = 0x1.8p0L };\n"
           "static long double _Complex extended_components = { 0x1.8p0L, -0x1.8p0L };\n"
           "int f(void) { return values[0] + (address != 0) + __builtin_types_compatible_p(int, long); }\n");
    String8 sources[] = {
        valid_source,
        valid_source,
        S8("enum { invalid = 1 / 0 };\n"),
    };
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(sources); index += 1)
    {
        Target target = target_native;
        if (index == 1) target.cpu_arch = CPU_ARCH_AARCH64;
        bool invalid = index + 1 == BUSTER_ARRAY_LENGTH(sources);
        CPreprocessResult preprocess = c_preprocess(temporary.arena, sources[index], (CPreprocessOptions){
            .source_path = S8("ast-test.c"), .target = target, .data_layout = target_data_layout(target), .dialect = C_PREPROCESS_DIALECT_GNU17,
        });
        CAstResult built = c_ast_build(temporary.arena, preprocess, (CAstOptions){0});
        CAstFacts facts = {0};
        if (BUSTER_REQUIRE(arguments, built.complete && c_ast_facts_create(&facts, &built.ast)))
        {
            CParserResult syntax = c_parse_ast(temporary.arena, preprocess);
            syntax.ast = &built.ast;
            syntax.ast_facts = &facts;
            syntax.type_interning = true;
            CAnalysisResult analysis = c_analyze_semantics_only(temporary.arena, preprocess, syntax);
            BUSTER_TEST(arguments, analysis.analysis_complete && (invalid ? analysis.diagnostic_count != 0 : analysis.diagnostic_count == 0));
            bool seven = false;
            bool zero = false;
            bool nine = false;
            bool fault = false;
            bool real = false;
            bool wide = false;
            bool extended = false;
            bool eleven = false;
            bool thirteen = false;
            bool identity = false;
            u32 selections = 0;
            u32 extended_initializer_count = 0;
            for (u32 node = 0; node < built.ast.node_count; node += 1)
            {
                CAstConstantFact value = c_ast_facts_constant(&facts, node);
                BUSTER_TEST(arguments, value.type.value == C_ID_UNDERLYING_INVALID || value.type.value < analysis.type_count);
                if (value.status == C_AST_CONSTANT_INTEGER)
                {
                    seven |= value.low == 7 && !value.high;
                    zero |= value.low == 0 && !value.high;
                    nine |= value.low == 9 && !value.high;
                    eleven |= value.low == 11 && !value.high;
                    thirteen |= value.low == 13 && !value.high;
                }
                u32 selected = c_ast_facts_selection(&facts, node);
                if (selected != C_AST_NODE_INVALID)
                {
                    BUSTER_TEST(arguments, selected >= c_ast_subtree_begin(&built.ast, node) && selected < node &&
                                           (built.ast.kinds[node] == C_AST_CALL || built.ast.kinds[node] == C_AST_GENERIC_SELECTION));
                    selections += 1;
                }
                if (built.ast.kinds[node] == C_AST_CALL)
                {
                    u32 callee = c_ast_child_at(&built.ast, node, 0);
                    String8 name = c_token_spelling(preprocess.spelling_base, preprocess.tokens[built.ast.tokens[callee]]);
                    identity |= string_equal(name, S8("__builtin_types_compatible_p")) && value.status == C_AST_CONSTANT_INTEGER && !value.low && !value.high;
                }
                if (value.status == C_AST_CONSTANT_FLOAT)
                {
                    real |= value.float_width == 64 && value.floating == 3.75;
                    wide |= value.float_width == 128 && !value.low && value.high == UINT64_C(0x3fff800000000000);
                }
                CAstExt80InitializerFact const* initializer = c_ast_facts_ext80_initializer(&facts, node);
                if (initializer)
                {
                    extended_initializer_count += 1;
                    extended |= initializer->significand == UINT64_C(0xc000000000000000) && initializer->exponent_sign == UINT16_C(0x3fff);
                    BUSTER_TEST(arguments, !value.offsetof_plus_one);
                }
                fault |= value.status == C_AST_CONSTANT_INVALID;
            }
            bool native_ext80 = target_data_layout(preprocess.target).long_double_type.bit_width == 80;
            bool native_float128 = target_data_layout(preprocess.target).long_double_type.bit_width == 128;
            BUSTER_TEST(arguments, invalid ? fault : seven && zero && nine && eleven && thirteen && identity && selections == 2 && real &&
                                                  (!native_float128 || wide) && (!native_ext80 || extended) && !fault);
            if (!invalid) BUSTER_TEST(arguments, native_ext80 ? extended_initializer_count >= 6 : extended_initializer_count == 0);
            BUSTER_TEST(arguments, facts.constant_count && facts.constant_count <= facts.node_count && facts.constants_arena);
            BUSTER_TEST(arguments, c_ast_facts_constant(&facts, C_AST_NODE_INVALID).status == C_AST_CONSTANT_UNQUERIED);
            BUSTER_TEST(arguments, !c_ast_facts_ext80_initializer(&facts, C_AST_NODE_INVALID));
            BUSTER_TEST(arguments, !c_ast_facts_offsetof(&facts, C_AST_NODE_INVALID));
            c_ast_facts_release(&facts);
            BUSTER_TEST(arguments, !facts.constants_arena && !facts.constant_slots && !facts.constants &&
                                   !facts.offsetof_plans && !facts.offsetof_steps && !facts.ext80_initializers);
        }
        c_ast_facts_release(&facts);
        c_ast_release(&built.ast);
    }
    c_test_scratch_end(temporary);
    return result;
}

// Lowering consumes declaration-point assertion verdicts. It accepts the
// validated source, preserves semantic failure diagnostics, and refuses a
// missing or altered verdict before creating any canonical IR.
BUSTER_GLOBAL_LOCAL UnitTestResult c_ast_test_static_assert_authority(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    TemporalArena temporary = scratch_begin(&arguments->arena, 1);
    Arena* arena = temporary.arena;
    CPreprocessResult preprocess = c_ast_test_preprocess(arena,
        S8("enum { E = 3 };\n_Static_assert(E == 3, \"enum\");\nint f(void) { return E; }\n"), C_PREPROCESS_DIALECT_GNU17);
    CParseResult analysis = c_parse_for_lowering(arena, preprocess);
    bool complete = analysis.analysis_complete && analysis.lowering_validated && !analysis.diagnostic_count &&
                    analysis.deferred_static_assert_count == 1 && analysis.deferred_static_assert_verdicts &&
                    analysis.deferred_static_assert_verdicts[0] == C_STATIC_ASSERT_HOLDS;
    BUSTER_TEST_RAW(arguments, complete, analysis.diagnostic_count ? analysis.diagnostics[0].message : S8("unfinished passing assertion"));
    if (complete)
    {
        CIRLowerResult lowered = c_lower_to_ir(arena, S8("passing-assertion.c"), preprocess, analysis, target_native);
        BUSTER_TEST(arguments, lowered.program && lowered.diagnostic_count == 0);
        u8 const unfinished[] = {C_STATIC_ASSERT_UNCHECKED, C_STATIC_ASSERT_FAILED, C_STATIC_ASSERT_NOT_CONSTANT};
        for (u32 form = 0; form < BUSTER_ARRAY_LENGTH(unfinished) + 1; form += 1)
        {
            CParseResult missing = analysis;
            u8 verdict = form ? unfinished[form - 1] : C_STATIC_ASSERT_HOLDS;
            missing.deferred_static_assert_verdicts = form ? &verdict : 0;
            CIRLowerResult refused = c_lower_to_ir(arena, S8("unfinished-assertion.c"), preprocess, missing, target_native);
            BUSTER_TEST(arguments, refused.program == 0 && refused.diagnostic_count == 1 &&
                                  refused.diagnostics[0].kind == C_DIAGNOSTIC_UNSUPPORTED_SEMANTICS &&
                                  refused.diagnostics[0].location.line == 2);
            BUSTER_TEST(arguments, analysis.deferred_static_assert_verdicts[0] == C_STATIC_ASSERT_HOLDS);
        }
    }
    CPreprocessResult failed_preprocess = c_ast_test_preprocess(arena,
        S8("enum { E = 3 };\n_Static_assert(E == 4, \"false enum\");\nint f(void) { return E; }\n"), C_PREPROCESS_DIALECT_GNU17);
    CParseResult failed = c_parse_for_lowering(arena, failed_preprocess);
    BUSTER_TEST(arguments, failed.diagnostic_count == 1 && failed.diagnostics[0].kind == C_DIAGNOSTIC_STATIC_ASSERT_FAILED);
    CIRLowerResult refused = c_lower_to_ir(arena, S8("false-assertion.c"), failed_preprocess, failed, target_native);
    BUSTER_TEST(arguments, refused.program == 0 && refused.diagnostic_count == failed.diagnostic_count &&
                          refused.diagnostics == failed.diagnostics);
    c_test_scratch_end(temporary);
    return result;
}

// Function ownership follows finished scopes, including evaluated parameter
// bounds. Global initializer bindings retain targets while names in member
// designators and nested prototypes do not create outside-body references.
BUSTER_GLOBAL_LOCAL UnitTestResult c_ast_test_unit_owner_plans(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    typedef struct CAstUnitOwnerCase CAstUnitOwnerCase;
    struct CAstUnitOwnerCase
    {
        String8 source;
        u32 functions;
        bool refused;
    };
    String8 const names[] = {S8("bound"), S8("owner"), S8("live"), S8("callback")};
    CAstUnitOwnerCase const cases[] = {
        {S8("static int bound(int n) { return n + 1; }\n"
            "static int owner(int n, int a[bound(n)]) { return a[0]; }\n"
            "int live(void) { return 0; }\n"), 4, false},
        {S8("static int bound(int n) { return n + 1; }\n"
            "int owner(int n, int a[bound(n)]) { return a[0]; }\n"
            "int live(void) { return 0; }\n"), 7, false},
        {S8("static int callback(void) { return 7; }\n"
            "int (*table[])(void) = { callback };\n"
            "extern int key; int *keys[] = { &key };\n"
            "extern int field; struct Record { int field; };\n"
            "struct Record record = { .field = 5 };\n"
            "int live(void) { return 0; }\n"), 12, false},
        {S8("static int callback(void) { return 7; }\n"
            "static int owner(void) { int prototype(int a[callback()]); return 0; }\n"
            "int live(void) { return 0; }\n"), 4, false},
        {S8("int live(int n) { return ({ if (n) goto in; in: n += 1; n; }); }\n"), 4, false},
        {S8("int live(int n) { int value = ({ if (n) goto out; 3; }); out: return value; }\n"), 4, false},
        {S8("int live(int n) { if (n) goto in; return ({ in: 3; }); }\n"), 0, true},
        {S8("int live(int n) { int a = ({ if (n) goto in; 1; }); int b = ({ in: 2; }); return a + b; }\n"), 0, true},
        {S8("int live(int n) { void *target = &&out; if (n) goto *target; out: return 3; }\n"), 4, false},
        {S8("int live(int n) { return _Generic(n, int: 1, default: 0) + _Generic(n, int: 2, default: 0) + "
            "_Generic(n, int: 3, default: 0) + _Generic(n, int: 4, default: 0); }\n"), 4, false},
    };
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(cases); index += 1)
    {
        for (u32 form = 0; form < 2; form += 1)
        {
            TemporalArena temporary = scratch_begin(&arguments->arena, 1);
            CPreprocessResult preprocess = c_ast_test_preprocess(temporary.arena, cases[index].source, C_PREPROCESS_DIALECT_GNU17);
            CParseResult analysis = c_parse_for_lowering(temporary.arena, preprocess);
            BUSTER_TEST_RAW(arguments, analysis.analysis_complete && !analysis.diagnostic_count,
                string_format(temporary.arena, S8("unit owner case={u32} form={u32}: {S8}"), index, form,
                    analysis.diagnostic_count ? analysis.diagnostics[0].message : S8("incomplete semantic analysis")));
            if (!analysis.diagnostic_count)
            {
                CIRLowerResult lowered = c_lower_to_ir_with_options(temporary.arena, S8("unit-owner-plans.c"), preprocess,
                    analysis, target_native, (CIRLowerOptions){.disable_direct_ssa = form != 0});
                BUSTER_TEST_RAW(arguments, cases[index].refused ? lowered.diagnostic_count == 1 :
                    lowered.diagnostic_count == 0 && lowered.program && lowered.canonical_ir_certified,
                    string_format(temporary.arena, S8("unit owner case={u32} form={u32}: {S8}"), index, form,
                        lowered.diagnostic_count ? lowered.diagnostics[0].message : S8("missing canonical program")));
                if (!cases[index].refused && lowered.program)
                {
                    IrModule* module = lowered.program->modules;
                    u32 mask = 0;
                    for (u32 function = 0; function < module->function_count; function += 1)
                        for (u32 name = 0; name < BUSTER_ARRAY_LENGTH(names); name += 1)
                            mask |= string_equal(module->functions[function].name, names[name]) ? 1u << name : 0;
                    BUSTER_TEST_RAW(arguments, mask == cases[index].functions,
                        string_format(temporary.arena, S8("unit owner case={u32} form={u32} expected={u32} found={u32}"),
                            index, form, cases[index].functions, mask));
                    if (index == 0 && form == 0)
                    {
                        CAstFacts missing_facts = *analysis.ast_facts;
                        missing_facts.matches = 0;
                        CParseResult missing = analysis;
                        missing.ast_facts = &missing_facts;
                        CIRLowerResult refused = c_lower_to_ir(temporary.arena, S8("missing-unit-provenance.c"), preprocess, missing, target_native);
                        BUSTER_TEST(arguments, refused.program == 0 && refused.diagnostic_count == 1 &&
                                              refused.diagnostics[0].kind == C_DIAGNOSTIC_UNSUPPORTED_SEMANTICS);
                    }
                    if (index == 2)
                    {
                        u32 key = 0;
                        u32 field = 0;
                        for (u32 symbol = 0; symbol < lowered.program->symbols.count; symbol += 1)
                        {
                            IrSymbol const* entry = ir_symbol_from_id(&lowered.program->symbols, (IrSymbolId){.value = symbol});
                            key += string_equal(entry->name, S8("key"));
                            field += string_equal(entry->name, S8("field"));
                        }
                        BUSTER_TEST(arguments, key == 1 && field == 0);
                    }
                }
            }
            c_test_scratch_end(temporary);
        }
    }
    return result;
}

// Intrinsic reservation walks evaluated nodes: storage-half values are
// rejected only when used, and vector expansions keep sufficient capacity.
BUSTER_GLOBAL_LOCAL UnitTestResult c_ast_test_vendor_budget_nodes(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    typedef struct CAstVendorBudgetCase CAstVendorBudgetCase;
    struct CAstVendorBudgetCase
    {
        String8 source;
        String8 refusal;
    };
    CAstVendorBudgetCase const cases[] = {
        {S8("int live(unsigned short n) { return sizeof(__builtin_bit_cast(__fp16, n)); }\n"), {0}},
        {S8("int live(int n) { return sizeof((__fp16)n); }\n"), {0}},
        {S8("int live(unsigned short n) { return sizeof(int[sizeof(__builtin_bit_cast(__fp16,n))]); }\n"), {0}},
        {S8("int live(int n) { return (int)(__fp16)n; }\n"), S8("__fp16 cast expression has no canonical implementation")},
        {S8("int live(unsigned short n) { return (int)__builtin_bit_cast(__fp16,n); }\n"),
         S8("__builtin_bit_cast destination __fp16 has no canonical implementation")},
        {S8("int live(int n) { return sizeof(int[n + (int)__builtin_bit_cast(__fp16,(unsigned short)0)]); }\n"),
         S8("__builtin_bit_cast destination __fp16 has no canonical implementation")},
        {S8("static int unused(int n) { return (int)(__fp16)n; } int live(void) { return 0; }\n"), {0}},
        {S8("typedef int V4i __attribute__((vector_size(16))); typedef float V4f __attribute__((vector_size(16))); "
            "V4f live(V4i a) { return __builtin_convertvector(a,V4f); }\n"), {0}},
        {S8("typedef int V4i __attribute__((vector_size(16))); "
            "V4i live(V4i a,V4i b) { return __builtin_shufflevector(a,b,0,3,4,7); }\n"), {0}},
    };
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(cases); index += 1)
    {
        for (u32 form = 0; form < 2; form += 1)
        {
            TemporalArena temporary = scratch_begin(&arguments->arena, 1);
            CPreprocessResult preprocess = c_ast_test_preprocess(temporary.arena, cases[index].source, C_PREPROCESS_DIALECT_GNU17);
            CParseResult analysis = c_parse_for_lowering(temporary.arena, preprocess);
            BUSTER_TEST_RAW(arguments, analysis.analysis_complete && !analysis.diagnostic_count,
                analysis.diagnostic_count ? analysis.diagnostics[0].message : S8("unfinished intrinsic analysis"));
            if (!analysis.diagnostic_count)
            {
                CIRLowerResult lowered = c_lower_to_ir_with_options(temporary.arena, S8("vendor-budget-nodes.c"), preprocess,
                    analysis, target_native, (CIRLowerOptions){.disable_direct_ssa = form != 0});
                bool refused = cases[index].refusal.length != 0;
                BUSTER_TEST_RAW(arguments, refused ? lowered.diagnostic_count == 1 &&
                    string_equal(lowered.diagnostics[0].message, cases[index].refusal) :
                    lowered.diagnostic_count == 0 && lowered.program && lowered.canonical_ir_certified,
                    string_format(temporary.arena, S8("intrinsic budget case={u32} form={u32}: {S8}"), index, form,
                        lowered.diagnostic_count ? lowered.diagnostics[0].message : S8("missing canonical program")));
            }
            c_test_scratch_end(temporary);
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult c_ast_test_symbol_bindings(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    TemporalArena temporary = scratch_begin(&arguments->arena, 1);
    Arena* arena = temporary.arena;
    String8 source = S8("int plain = 0;\n"
                        "int hidden __attribute__((visibility(\"hid\" \"den\")));\n"
                        "int weakened __attribute__((weak));\n"
                        "int aliased_object __attribute__((alias(\"plain\")));\n"
                        "void ctor(void) __attribute__((constructor(101)));\n"
                        "void doomed(void) __attribute__((error(\"bad\\n\" \"call\")));\n"
                        "int renamed asm(\"link_name\");\n"
                        "int sectioned __attribute__((section(\".payload\")));\n"
                        "_Noreturn void stopped(void);\n"
                        "inline __attribute__((gnu_inline)) int inlined(void) { return 1; }\n"
                        "void locals(void) {\n"
                        " static int one __attribute__((section(\".local\"))) = 1;\n"
                        " static int first __attribute__((section(\".first\"))) = 2, next = 3;\n"
                        " extern int imported __attribute__((visibility(\"hidden\")));\n"
                        "}\n");
    CPreprocessResult preprocess = c_ast_test_preprocess(arena, source, C_PREPROCESS_DIALECT_GNU17);
    CParseResult analysis = c_parse_for_lowering(arena, preprocess);
    BUSTER_TEST_RAW(arguments, analysis.analysis_complete && analysis.lowering_validated && analysis.diagnostic_count == 0 &&
                                   analysis.declaration_bindings_complete,
                    string_format(arena, S8("bindings complete={u32} validated={u32} rows={u32} diagnostic={S8}"),
                                  (u32)analysis.declaration_bindings_complete, (u32)analysis.lowering_validated,
                                  analysis.declaration_binding_count, analysis.diagnostic_count ? analysis.diagnostics[0].message : S8("none")));
    CParseResult missing = analysis;
    missing.declaration_bindings_complete = false;
    CIRLowerResult refused = c_lower_to_ir(arena, S8("missing-symbol-properties.c"), preprocess, missing, target_native);
    BUSTER_TEST(arguments, refused.program == 0 && refused.diagnostic_count == 1 &&
                               refused.diagnostics[0].kind == C_DIAGNOSTIC_UNSUPPORTED_SEMANTICS);
    u32 checked = 0;
    for (u32 index = 0; index < analysis.declaration_count; index += 1)
    {
        CDeclaration const* declaration = analysis.declarations + index;
        u32 row = declaration->binding_plus_one - 1;
        if (string_equal(declaration->name, S8("plain")))
        {
            BUSTER_TEST(arguments, row == C_AST_NODE_INVALID);
            checked += 1;
        }
        if (row < analysis.declaration_binding_count)
        {
            CDeclarationBinding const* binding = analysis.declaration_bindings + row;
            if (string_equal(declaration->name, S8("hidden")))
            {
                BUSTER_TEST(arguments, binding->visibility == C_SYMBOL_VISIBILITY_HIDDEN && !binding->visibility_invalid);
                checked += 1;
            }
            if (string_equal(declaration->name, S8("weakened")))
            {
                BUSTER_TEST(arguments, binding->is_weak);
                checked += 1;
            }
            if (string_equal(declaration->name, S8("aliased_object")))
            {
                BUSTER_TEST(arguments, string_equal(binding->alias_target, S8("plain")));
                checked += 1;
            }
            if (string_equal(declaration->name, S8("ctor")))
            {
                BUSTER_TEST(arguments, binding->is_constructor && binding->constructor_priority == 101);
                checked += 1;
            }
            if (string_equal(declaration->name, S8("doomed")))
            {
                BUSTER_TEST(arguments, binding->error_attribute_token_plus_one && string_equal(binding->error_message, S8("bad\\ncall")));
                checked += 1;
            }
            if (string_equal(declaration->name, S8("renamed")))
            {
                BUSTER_TEST(arguments, string_equal(binding->link_name, S8("link_name")));
                checked += 1;
            }
            if (string_equal(declaration->name, S8("sectioned")))
            {
                BUSTER_TEST(arguments, string_equal(binding->section_name, S8(".payload")));
                checked += 1;
            }
            if (string_equal(declaration->name, S8("stopped")))
            {
                BUSTER_TEST(arguments, binding->is_noreturn);
                checked += 1;
            }
            if (string_equal(declaration->name, S8("inlined")))
            {
                BUSTER_TEST(arguments, binding->has_gnu_inline_attribute);
                checked += 1;
            }
        }
    }
    for (u32 index = 0; index < analysis.entity_count; index += 1)
    {
        CEntity const* entity = analysis.entities + index;
        u32 row = entity->binding_plus_one - 1;
        if (entity->kind == C_ENTITY_LOCAL && string_equal(entity->name, S8("next")))
        {
            BUSTER_TEST(arguments, row == C_AST_NODE_INVALID);
            checked += 1;
        }
        if (row < analysis.declaration_binding_count)
        {
            CDeclarationBinding const* binding = analysis.declaration_bindings + row;
            if (string_equal(entity->name, S8("one")))
            {
                BUSTER_TEST(arguments, string_equal(binding->section_name, S8(".local")));
                checked += 1;
            }
            if (string_equal(entity->name, S8("first")))
            {
                BUSTER_TEST(arguments, string_equal(binding->section_name, S8(".first")));
                checked += 1;
            }
            if (string_equal(entity->name, S8("imported")) && entity->kind == C_ENTITY_LOCAL)
            {
                BUSTER_TEST(arguments, binding->visibility == C_SYMBOL_VISIBILITY_HIDDEN);
                checked += 1;
            }
        }
    }
    BUSTER_TEST_RAW(arguments, checked == 14, string_format(arena, S8("symbol binding checked={u32} rows={u32}"), checked, analysis.declaration_binding_count));
    BUSTER_TEST(arguments, analysis.declaration_binding_count == 12 &&
                               analysis.declaration_binding_copy_bytes >= (u64)analysis.declaration_binding_count * sizeof(CDeclarationBinding) &&
                               analysis.declaration_binding_retained_bytes >= (u64)analysis.declaration_binding_count * sizeof(CDeclarationBinding));
    String8 growth = {0};
    for (u32 index = 0; index < 70; index += 1)
    {
        growth = string_format(arena, S8("{S8}void grow_{u32}(void) __attribute__((returns_twice));\n"), growth, index);
    }
    CPreprocessResult growth_preprocess = c_ast_test_preprocess(arena, growth, C_PREPROCESS_DIALECT_GNU17);
    CParseResult grown = c_parse_for_lowering(arena, growth_preprocess);
    BUSTER_TEST(arguments, grown.diagnostic_count == 0 && grown.declaration_binding_count == 70 &&
                               grown.declaration_binding_copy_bytes > (u64)grown.declaration_binding_count * sizeof(CDeclarationBinding));
    for (u32 index = 0; index < grown.declaration_count; index += 1)
    {
        u32 row = grown.declarations[index].binding_plus_one - 1;
        BUSTER_TEST(arguments, row < grown.declaration_binding_count && grown.declaration_bindings[row].is_returns_twice);
    }
    c_preprocess_release(&growth_preprocess);
    c_preprocess_release(&preprocess);
    c_test_scratch_end(temporary);
    return result;
}

// Explicit owner expectations stay separate from the token-reference model,
// whose declarations have source anchors but no syntax-node identities.
BUSTER_GLOBAL_LOCAL UnitTestResult c_ast_test_declaration_owners(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    String8 source = S8("struct T { int member; };\n"
                        "extern int shared;\n"
                        "int shared = 3, array[] = { 4, 5 };\n"
                        "int (*callback)(int parameter);\n"
                        "int named(int named_parameter);\n"
                        "int named(int named_parameter) {\n"
                        "  static int local[] = { 6, 7 };\n"
                        "  register int reg asm(\"r12\");\n"
                        "  int (*fn)(int shadow) = callback;\n"
                        "  return shared + local[0];\n"
                        "}\n"
                        "int oldstyle(a) int a; { return a; }\n"
                        "asm(\"\");\n");
    String8 const names[] = {S8("T"), S8("shared"), S8("shared"), S8("array"), S8("callback"), S8("named"), S8("named"), S8("oldstyle"), S8("")};
    u32 const owner_kinds[] = {C_AST_DECLARATION, C_AST_DECLARATION, C_AST_DECLARATION, C_AST_DECLARATION, C_AST_DECLARATION,
                              C_AST_DECLARATION, C_AST_FUNCTION_DEFINITION, C_AST_FUNCTION_DEFINITION, C_AST_ASM_TOP_LEVEL};
    u32 const initializer_kinds[] = {C_AST_KIND_COUNT, C_AST_KIND_COUNT, C_AST_NUMBER, C_AST_INITIALIZER_LIST, C_AST_KIND_COUNT,
                                    C_AST_KIND_COUNT, C_AST_KIND_COUNT, C_AST_KIND_COUNT, C_AST_KIND_COUNT};
    for (u32 layout = 0; layout <= C_AST_LAYOUT_EXPLICIT; layout += 1)
    {
        TemporalArena temporary = scratch_begin(&arguments->arena, 1);
        Arena* arena = temporary.arena;
        CPreprocessResult preprocess = c_ast_test_preprocess(arena, source, C_PREPROCESS_DIALECT_GNU17);
        CAstResult built = c_ast_build(arena, preprocess, (CAstOptions){.layout = (CAstLayout)layout, .refill_batch = layout + 1});
        CAstFacts facts = {0};
        if (BUSTER_REQUIRE(arguments, built.complete && c_ast_facts_create(&facts, &built.ast)))
        {
            CAst const* ast = &built.ast;
            CParserResult syntax = c_parse_ast(arena, preprocess);
            syntax.ast = ast;
            syntax.ast_facts = &facts;
            syntax.type_interning = true;
            CAnalysisResult analysis = c_analyze_semantics_only(arena, preprocess, syntax);
            BUSTER_TEST(arguments, analysis.analysis_complete && analysis.diagnostic_count == 0);
            BUSTER_TEST(arguments, analysis.declaration_count == BUSTER_ARRAY_LENGTH(names));
            for (u32 index = 0; index < analysis.declaration_count && index < BUSTER_ARRAY_LENGTH(names); index += 1)
            {
                CDeclaration const* declaration = analysis.declarations + index;
                u32 owner = declaration->declaration_node_plus_one - 1;
                u32 init = declaration->init_declarator_plus_one - 1;
                u32 initializer = declaration->initializer_node_plus_one - 1;
                BUSTER_TEST(arguments, string_equal(declaration->name, names[index]));
                bool retained = owner < ast->node_count;
                BUSTER_TEST(arguments, retained && ast->kinds[owner] == owner_kinds[index] && ast->tokens[owner] == declaration->token_start);
                bool has_init = index >= 1 && index <= 5;
                BUSTER_TEST(arguments, has_init == (init < ast->node_count));
                if (init < ast->node_count)
                {
                    u32 name = c_ast_declarator_name(ast, init);
                    BUSTER_TEST(arguments, ast->kinds[init] == C_AST_INIT_DECLARATOR && name < ast->node_count &&
                                               string_equal(c_token_spelling(preprocess.spelling_base, preprocess.tokens[ast->tokens[name]]), names[index]));
                    BUSTER_TEST(arguments, owner >= init && c_ast_subtree_begin(ast, owner) <= c_ast_subtree_begin(ast, init));
                }
                bool initialized = initializer_kinds[index] != C_AST_KIND_COUNT;
                BUSTER_TEST(arguments, initialized == (initializer < ast->node_count));
                if (initializer < ast->node_count)
                {
                    BUSTER_TEST(arguments, ast->kinds[initializer] == initializer_kinds[index] && initializer + 1 == init);
                }
            }
            u32 locals = 0;
            for (u32 entity = 0; entity < analysis.entity_count; entity += 1)
            {
                CEntity const* binding = analysis.entities + entity;
                u32 init = binding->init_declarator_plus_one - 1;
                if (init < ast->node_count)
                {
                    u32 name = c_ast_declarator_name(ast, init);
                    BUSTER_TEST(arguments, ast->kinds[init] == C_AST_INIT_DECLARATOR && name < ast->node_count &&
                                               (facts.flags[name] & C_AST_FACT_BOUND) && facts.payloads[name] == entity &&
                                               ast->tokens[name] + 1 == binding->declaration_token_plus_one);
                    u32 owner = binding->declaration_node_plus_one - 1;
                    BUSTER_TEST(arguments, owner < ast->node_count && ast->kinds[owner] == C_AST_DECLARATION && init < owner &&
                                               c_ast_subtree_begin(ast, owner) <= c_ast_subtree_begin(ast, init));
                }
                if (binding->kind == C_ENTITY_LOCAL)
                {
                    locals += 1;
                    BUSTER_TEST(arguments, init < ast->node_count);
                    if (string_equal(binding->name, S8("reg")) && init < ast->node_count)
                    {
                        BUSTER_TEST(arguments, (ast->data[init] & 1u) && ast->kinds[c_ast_child_at(ast, init, 1)] == C_AST_ASM_LABEL);
                    }
                }
            }
            BUSTER_TEST(arguments, locals == 3);
        }
        c_ast_facts_release(&facts);
        c_ast_release(&built.ast);
        c_test_scratch_end(temporary);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult c_ast_test_retained_facts(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    TemporalArena temporary = scratch_begin(&arguments->arena, 1);
    Arena* arena = temporary.arena;
    String8 source = S8("struct S { int a : 3; int b; };\n"
                        "char const* names[] = { \"one\", \"three\" };\n"
                        "int table[] = { 1, 2, 3 };\n"
                        "int f(struct S* s, int i)\n"
                        "{\n"
                        "    int x = s->a + s->b;\n"
                        "    x += (int)sizeof(\"text\");\n"
                        "    x += \"abc\"[i];\n"
                        "    x += __builtin_expect(i, 0);\n"
                        "    char const* name = __func__;\n"
                        "    return x + table[i] + (name != 0);\n"
                        "}\n");
    CPreprocessResult preprocess = c_ast_test_preprocess(arena, source, C_PREPROCESS_DIALECT_GNU17);
    CAstResult built = c_ast_build(arena, preprocess, (CAstOptions){0});
    CAstFacts facts = {0};
    bool ready = BUSTER_REQUIRE(arguments, preprocess.error_count == 0 && built.complete) &&
                 BUSTER_REQUIRE(arguments, c_ast_facts_create(&facts, &built.ast));
    if (ready)
    {
        CAst const* ast = &built.ast;
        BUSTER_TEST(arguments, facts.node_count == ast->node_count && facts.statistics.retained_bytes >= (u64)ast->node_count * 9);
        CParserResult plain_syntax = c_parse_ast_reference(arena, preprocess);
        plain_syntax.ast = ast;
        plain_syntax.type_interning = true;
        CAnalysisResult plain = c_analyze_semantics_only(arena, preprocess, plain_syntax);
        CParserResult syntax = c_parse_ast_reference(arena, preprocess);
        syntax.ast = ast;
        syntax.type_interning = true;
        syntax.ast_facts = &facts;
        CAnalysisResult analysis = c_analyze_semantics_only(arena, preprocess, syntax);
        BUSTER_TEST(arguments, analysis.analysis_complete && analysis.diagnostic_count == 0);
        BUSTER_TEST(arguments, facts.spans && facts.index_arena && facts.matches);
        for (u32 node = 0; node < ast->node_count; node += 1)
        {
            if (ast->kinds[node] == C_AST_DECLARATOR_NAME)
            {
                BUSTER_TEST(arguments, c_ast_facts_locate(&facts, &preprocess, ast->tokens[node], ast->tokens[node] + 1) == node);
            }
        }
        BUSTER_TEST(arguments, c_ast_facts_locate(&facts, &preprocess, 0, (u32)preprocess.token_count + 1) == C_AST_NODE_INVALID);
        String8 difference = c_ast_corpus_analyses_differ(arena, &plain, &analysis);
        difference = difference.length ? difference : c_ast_corpus_model_differ(arena, &plain, &analysis);
        BUSTER_TEST_RAW(arguments, difference.length == 0, difference);
        CAstFactStatistics statistics = facts.statistics;
        u64 gaps = 0;
        for (u32 gap = 0; gap < C_AST_FACT_GAP_COUNT; gap += 1)
        {
            gaps += statistics.gaps[gap];
        }
        BUSTER_TEST(arguments, statistics.regions == 3 && statistics.candidates);
        BUSTER_TEST(arguments, statistics.tree + statistics.machine + statistics.completed + gaps == statistics.candidates);
        BUSTER_TEST(arguments, statistics.stale == 0 && statistics.bit_fields == 1 && statistics.strings == 4 && statistics.bound);
        BUSTER_TEST(arguments, statistics.gaps[C_AST_FACT_GAP_INVALID] == 0 && statistics.gaps[C_AST_FACT_GAP_APPENDS] == 0 &&
                                   statistics.builtin_identities == 1 && statistics.function_names == 1);
        u32 checked = 0;
        for (u32 node = 0; node < ast->node_count; node += 1)
        {
            u32 flags = facts.flags[node];
            u32 type = facts.types[node];
            u32 kind = ast->kinds[node];
            String8 spelling = c_token_spelling(preprocess.spelling_base, preprocess.tokens[ast->tokens[node]]);
            BUSTER_TEST(arguments, !(flags & C_AST_FACT_TYPED) || ((flags & C_AST_FACT_RECIPE) ? type < facts.recipe_count : type < analysis.type_count));
            CTypeKind type_kind = (flags & C_AST_FACT_TYPED) && !(flags & C_AST_FACT_RECIPE) && type < analysis.type_count ? analysis.types[type].kind : C_TYPE_COUNT;
            if (kind == C_AST_STRING && string_equal(spelling, S8("\"abc\"")))
            {
                BUSTER_TEST(arguments, (flags & C_AST_FACT_STRING) && type_kind == C_TYPE_CHAR && facts.payloads[node] == 4);
                checked += 1;
            }
            if (kind == C_AST_INDEX && (flags & C_AST_FACT_EXPRESSION) && c_ast_subtree_begin(ast, node) + 1 < node &&
                ast->kinds[c_ast_subtree_begin(ast, node)] == C_AST_STRING)
            {
                BUSTER_TEST(arguments, (flags & C_AST_FACT_TYPED) && type_kind == C_TYPE_CHAR);
                checked += 1;
            }
            if (kind == C_AST_MEMBER_ARROW && (flags & C_AST_FACT_BIT_FIELD))
            {
                BUSTER_TEST(arguments, facts.payloads[node] == 3 && type_kind == C_TYPE_INT);
                checked += 1;
            }
            if (kind == C_AST_IDENTIFIER && string_equal(spelling, S8("s")) && (flags & C_AST_FACT_EXPRESSION))
            {
                u32 entity = facts.payloads[node];
                BUSTER_TEST(arguments, (flags & C_AST_FACT_BOUND) && entity < analysis.entity_count &&
                                           analysis.entities[entity].kind == C_ENTITY_PARAMETER && type_kind == C_TYPE_POINTER);
                checked += 1;
            }
            if (kind == C_AST_IDENTIFIER && string_equal(spelling, S8("__func__")))
            {
                bool recipe = (flags & (C_AST_FACT_EXPRESSION | C_AST_FACT_TYPED | C_AST_FACT_RECIPE)) ==
                              (C_AST_FACT_EXPRESSION | C_AST_FACT_TYPED | C_AST_FACT_RECIPE) && type < facts.recipe_count;
                BUSTER_TEST(arguments, recipe);
                if (recipe)
                {
                    CAstTypeRecipe const* description = facts.recipes + type;
                    BUSTER_TEST(arguments, description->form == C_AST_TYPE_RECIPE_FUNCTION_NAME && description->bound.has_inferred_count &&
                                               description->bound.inferred_count == 2 && description->type.kind == C_TYPE_ARRAY && description->type.is_const &&
                                               description->type.element_type.value < analysis.type_count &&
                                               analysis.types[description->type.element_type.value].kind == C_TYPE_CHAR);
                }
                checked += 1;
            }
            if (kind == C_AST_IDENTIFIER && string_equal(spelling, S8("__builtin_expect")))
            {
                bool recipe = (flags & C_AST_FACT_RECIPE) && type < facts.recipe_count;
                BUSTER_TEST(arguments, recipe);
                if (recipe)
                {
                    BUSTER_TEST(arguments, facts.recipes[type].form == C_AST_TYPE_RECIPE_BUILTIN && facts.recipes[type].builtin != 0 &&
                                               facts.recipes[type].type.kind == C_TYPE_INVALID);
                }
                checked += 1;
            }
            if (kind == C_AST_DECLARATOR_NAME && string_equal(spelling, S8("x")))
            {
                u32 entity = facts.payloads[node];
                bool bound = (flags & C_AST_FACT_BOUND) && entity < analysis.entity_count;
                BUSTER_TEST(arguments, bound && analysis.entities[entity].kind == C_ENTITY_LOCAL &&
                                           analysis.entities[entity].declaration_token_plus_one == ast->tokens[node] + 1);
                u32 assignment = bound ? analysis.entities[entity].initializer_token_plus_one : 0;
                BUSTER_TEST(arguments, assignment && assignment <= preprocess.token_count &&
                                           preprocess.tokens[assignment - 1].kind == C_TOKEN_PUNCTUATOR &&
                                           preprocess.tokens[assignment - 1].punctuator == C_PUNCTUATOR_ASSIGN);
                checked += 1;
            }
            if (kind == C_AST_NUMBER && string_equal(spelling, S8("2")))
            {
                BUSTER_TEST(arguments, (flags & C_AST_FACT_EXPRESSION) && type_kind == C_TYPE_INT && !(flags & C_AST_FACT_MACHINE));
                checked += 1;
            }
        }
        // "abc", its `[]`, s->a, two uses of s, the builtin, __func__, x and table's 2.
        BUSTER_TEST(arguments, checked == 9);
        c_ast_facts_release(&facts);
        c_ast_facts_release(&facts);
        BUSTER_TEST(arguments, facts.flags == 0 && facts.arena == 0 && facts.index_arena == 0 && facts.spans == 0 && facts.recipe_arena == 0 && facts.recipes == 0 && facts.statistics.stale == 0);
    }
    c_ast_facts_release(&facts);
    c_ast_release(&built.ast);
    c_test_scratch_end(temporary);
    return result;
}

// Completion descriptions remain independent of the type table they were
// queried against, including a qualified element and an array-decay pointer.
// Declared intrinsics keep CALL identity without replacing their ordinary
// function-designator type. The sealed payload survives private-arena release.
BUSTER_GLOBAL_LOCAL UnitTestResult c_ast_test_call_builtin_facts(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    for (u32 layout = 0; layout < BUSTER_ARRAY_LENGTH(c_ast_test_layouts); layout += 1)
    {
        TemporalArena temporary = scratch_begin(&arguments->arena, 1);
        CPreprocessResult preprocess = c_ast_test_preprocess(temporary.arena,
            S8("void _mm_pause(void); int ordinary(void); void f(void) { "
               "void (*p)(void) = _mm_pause; _mm_pause(); __builtin_ia32_pause(); p(); (void)ordinary(); }"),
            C_PREPROCESS_DIALECT_GNU17);
        CAstResult built = c_ast_build(temporary.arena, preprocess, (CAstOptions){.layout = c_ast_test_layouts[layout]});
        CAstFacts facts = {0};
        if (BUSTER_REQUIRE(arguments, built.complete && c_ast_facts_create(&facts, &built.ast)))
        {
            CParserResult syntax = c_parse_ast_from_tree(temporary.arena, preprocess, &built.ast, 0);
            syntax.ast_facts = &facts;
            CAnalysisResult analysis = c_analyze_semantics_only(temporary.arena, preprocess, syntax);
            if (BUSTER_REQUIRE(arguments, analysis.analysis_complete && !analysis.diagnostic_count))
            {
                u32 calls = 0;
                u32 builtins = 0;
                u32 designators = 0;
                u32 builtin_family = 0;
                for (u32 node = 0; node < built.ast.node_count; node += 1)
                {
                    if (built.ast.kinds[node] == C_AST_CALL)
                    {
                        calls += 1;
                        u32 builtin = c_ast_facts_call_builtin(&built.ast, &facts, node);
                        u32 callee = c_ast_child_at(&built.ast, node, 0);
                        String8 name = c_token_spelling(preprocess.spelling_base, preprocess.tokens[built.ast.tokens[callee]]);
                        bool expected = string_equal(name, S8("_mm_pause")) || string_equal(name, S8("__builtin_ia32_pause"));
                        builtins += builtin != 0;
                        BUSTER_TEST(arguments, facts.payloads[node] != C_AST_FACT_NONE && (builtin != 0) == expected);
                        if (builtin)
                        {
                            BUSTER_TEST(arguments, !builtin_family || builtin_family == builtin);
                            builtin_family = builtin;
                        }
                    }
                    else if (built.ast.kinds[node] == C_AST_IDENTIFIER &&
                             string_equal(c_token_spelling(preprocess.spelling_base, preprocess.tokens[built.ast.tokens[node]]), S8("_mm_pause")))
                    {
                        designators += 1;
                        u32 flags = facts.flags[node];
                        u32 type = facts.types[node];
                        BUSTER_TEST(arguments, (flags & (C_AST_FACT_BOUND | C_AST_FACT_TYPED)) == (C_AST_FACT_BOUND | C_AST_FACT_TYPED) &&
                                               !(flags & C_AST_FACT_RECIPE) && type < analysis.type_count && analysis.types[type].kind == C_TYPE_FUNCTION);
                        BUSTER_TEST(arguments, c_ast_facts_call_builtin(&built.ast, &facts, node) == C_AST_FACT_NONE);
                    }
                }
                BUSTER_TEST(arguments, calls == 4 && builtins == 2 && designators == 2);
                BUSTER_TEST(arguments, c_ast_facts_retain_in_arena(temporary.arena, &facts) != 0 && !facts.arena);
                u32 sealed = 0;
                for (u32 node = 0; node < built.ast.node_count; node += 1)
                {
                    sealed += c_ast_facts_call_builtin(&built.ast, &facts, node) == builtin_family;
                }
                BUSTER_TEST(arguments, sealed == 2);
            }
        }
        c_ast_facts_release(&facts);
        c_ast_release(&built.ast);
        c_test_scratch_end(temporary);
    }
    return result;
}

// #4227: a member's name does not turn its function pointer into a builtin.
BUSTER_GLOBAL_LOCAL UnitTestResult c_ast_test_member_builtin_facts(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    for (u32 layout = 0; layout < BUSTER_ARRAY_LENGTH(c_ast_test_layouts); layout += 1)
    {
        TemporalArena temporary = scratch_begin(&arguments->arena, 1);
        CPreprocessResult preprocess = c_ast_test_preprocess(temporary.arena,
            S8("struct S { int (*__builtin_popcount)(int); int (*__builtin_expect)(int, int); }; "
               "int f(struct S value, struct S *pointer) { return value.__builtin_popcount(1) + "
               "pointer->__builtin_expect(2, 3) + __builtin_popcount(7); }"), C_PREPROCESS_DIALECT_GNU17);
        CAstResult built = c_ast_build(temporary.arena, preprocess, (CAstOptions){.layout = c_ast_test_layouts[layout]});
        if (BUSTER_REQUIRE(arguments, built.complete))
        {
            CParserResult syntax = c_parse_ast_from_tree(temporary.arena, preprocess, &built.ast, 0);
            CAnalysisResult analysis = c_analyze_semantics_only(temporary.arena, preprocess, syntax);
            if (BUSTER_REQUIRE(arguments, analysis.analysis_complete && !analysis.diagnostic_count && analysis.ast_facts))
            {
                u32 callbacks = 0;
                u32 builtins = 0;
                for (u32 node = 0; node < built.ast.node_count; node += 1)
                {
                    if (built.ast.kinds[node] == C_AST_CALL)
                    {
                        u32 callee = c_ast_child_at(&built.ast, node, 0);
                        u32 kind = built.ast.kinds[callee];
                        u32 builtin = c_ast_facts_call_builtin(&built.ast, analysis.ast_facts, node);
                        bool member = kind == C_AST_MEMBER || kind == C_AST_MEMBER_ARROW;
                        BUSTER_TEST(arguments, builtin != C_AST_FACT_NONE && (member ? builtin == 0 : builtin != 0));
                        callbacks += member;
                        builtins += !member;
                    }
                }
                BUSTER_TEST(arguments, callbacks == 2 && builtins == 1);
            }
        }
        c_ast_release(&built.ast);
        c_test_scratch_end(temporary);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult c_ast_test_offsetof_facts(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    String8 const sources[] = {
        S8("struct Leaf { char c; int b[4]; };\n"
           "struct S { char pad; struct Leaf a[3]; union { struct { int promoted; }; }; };\n"
           "enum { inline_offset = __builtin_offsetof(struct Inline { char c; int value; }, value) };\n"
           "enum { constant_offset = __builtin_offsetof(struct S, a[1].b[2]) };\n"
           "int f(int i, int j) { return __builtin_offsetof(struct S, a[i].b[j]) +\n"
           " __builtin_offsetof(struct S, promoted) +\n"
           " __builtin_offsetof(struct S, a[__builtin_offsetof(struct Leaf, b) / sizeof(int)].b[1]); }\n"),
        S8("struct Bad { int bit : 3; }; unsigned long value = __builtin_offsetof(struct Bad, bit);\n"),
        S8("struct Protected { char c; int a[4]; };\n"
           "typedef __typeof__(int[__builtin_offsetof(struct Protected, a[2]) + sizeof(int)]) ProtectedArray;\n"
           "int f(void) { return sizeof(__typeof__(int[__builtin_offsetof(struct Protected, a[1])])) + sizeof(ProtectedArray); }\n"),
    };
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(sources); index += 1)
    {
        for (u32 layout = 0; layout < BUSTER_ARRAY_LENGTH(c_ast_test_layouts); layout += 1)
        {
            TemporalArena temporary = scratch_begin(&arguments->arena, 1);
            CPreprocessResult preprocess = c_ast_test_preprocess(temporary.arena, sources[index], C_PREPROCESS_DIALECT_GNU17);
            CAstResult built = c_ast_build(temporary.arena, preprocess, (CAstOptions){.layout = c_ast_test_layouts[layout]});
            CAstFacts facts = {0};
            if (BUSTER_REQUIRE(arguments, built.complete && c_ast_facts_create(&facts, &built.ast)))
            {
                CParserResult syntax = c_parse_ast(temporary.arena, preprocess);
                syntax.ast = &built.ast;
                CAnalysisResult plain = c_analyze_semantics_only(temporary.arena, preprocess, syntax);
                syntax.ast_facts = &facts;
                c_test_offsetof_completion_only_set(index == 2);
                CAnalysisResult analysis = c_analyze_semantics_only(temporary.arena, preprocess, syntax);
                c_test_offsetof_completion_only_set(false);
                BUSTER_TEST(arguments, analysis.analysis_complete && (index == 1 ? analysis.diagnostic_count != 0 : analysis.diagnostic_count == 0));
                String8 difference = c_ast_corpus_analyses_differ(temporary.arena, &plain, &analysis);
                u64 rows = 0;
                difference = difference.length ? difference : c_ast_corpus_models_differ(temporary.arena, &plain, &analysis, &rows);
                BUSTER_TEST_RAW(arguments, difference.length == 0, difference);
                u32 plans = 0;
                u32 indexed = 0;
                bool promoted = false;
                bool inline_member = false;
                for (u32 node = 0; node < built.ast.node_count; node += 1)
                {
                    CAstOffsetofPlan const* plan = c_ast_facts_offsetof(&facts, node);
                    if (plan)
                    {
                        plans += 1;
                        BUSTER_TEST(arguments, built.ast.kinds[node] == C_AST_CALL && plan->valid == (index != 1));
                        BUSTER_TEST(arguments, plan->first_step <= facts.offsetof_step_count &&
                                               plan->step_count <= facts.offsetof_step_count - plan->first_step);
                        if (plan->valid && plan->step_count == 4)
                        {
                            CAstOffsetofStep const* steps = facts.offsetof_steps + plan->first_step;
                            BUSTER_TEST(arguments, steps[0].amount == 4 && steps[0].index_node == C_AST_NODE_INVALID &&
                                                   steps[1].amount == 20 && steps[1].index_node != C_AST_NODE_INVALID &&
                                                   steps[2].amount == 4 && steps[2].index_node == C_AST_NODE_INVALID &&
                                                   steps[3].amount == 4 && steps[3].index_node != C_AST_NODE_INVALID);
                            for (u32 part = 0; part < plan->step_count; part += 1)
                            {
                                if (steps[part].index_node != C_AST_NODE_INVALID)
                                {
                                    BUSTER_TEST(arguments, built.ast.kinds[steps[part].node] == C_AST_INDEX &&
                                                           c_ast_child_at(&built.ast, steps[part].node, 1) == steps[part].index_node);
                                }
                            }
                            indexed += 1;
                        }
                        else if (plan->valid && plan->step_count == 1)
                        {
                            CAstOffsetofStep const* step = facts.offsetof_steps + plan->first_step;
                            String8 member = c_token_spelling(preprocess.spelling_base, preprocess.tokens[built.ast.tokens[step->node]]);
                            promoted |= string_equal(member, S8("promoted")) && step->amount == 64;
                            inline_member |= string_equal(member, S8("value")) && step->amount == 4;
                        }
                    }
                }
                BUSTER_TEST(arguments, index == 1 ? plans == 1 : index == 2 ? plans == 2 : plans == 6 && indexed == 3 && promoted && inline_member);
                BUSTER_TEST(arguments, facts.offsetof_plan_count == plans && facts.offsetof_step_count <= facts.node_count);
                if (index == 2)
                {
                    u32 constants = facts.constant_count;
                    u32 steps = facts.offsetof_step_count;
                    c_test_offsetof_completion_only_set(true);
                    CAnalysisResult repeated = c_analyze_semantics_only(temporary.arena, preprocess, syntax);
                    c_test_offsetof_completion_only_set(false);
                    difference = c_ast_corpus_analyses_differ(temporary.arena, &analysis, &repeated);
                    difference = difference.length ? difference : c_ast_corpus_models_differ(temporary.arena, &analysis, &repeated, &rows);
                    BUSTER_TEST_RAW(arguments, difference.length == 0, difference);
                    BUSTER_TEST(arguments, facts.constant_count == constants && facts.offsetof_plan_count == plans && facts.offsetof_step_count == steps);
                    for (u32 node = 0; node < built.ast.node_count; node += 1)
                    {
                        CAstOffsetofPlan const* plan = c_ast_facts_offsetof(&facts, node);
                        if (plan)
                        {
                            BUSTER_TEST(arguments, plan->valid && plan->step_count == 2);
                            CAstOffsetofStep const* parts = facts.offsetof_steps + plan->first_step;
                            BUSTER_TEST(arguments, parts[0].amount == 4 && parts[0].index_node == C_AST_NODE_INVALID &&
                                                   parts[1].amount == 4 && parts[1].index_node != C_AST_NODE_INVALID);
                            BUSTER_TEST(arguments, c_ast_facts_constant(&facts, node).type.value < analysis.type_count ||
                                                   c_ast_facts_constant(&facts, node).type.value == C_TYPE_ID_INVALID.value);
                        }
                    }
                }
            }
            c_ast_facts_release(&facts);
            c_ast_release(&built.ast);
            c_test_scratch_end(temporary);
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult c_ast_test_retained_type_recipes(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    String8 const sources[] = {
        S8("struct S { int v[4]; }; void f(struct S const* p, int i) { __builtin_constant_p(p->v[i]); }"),
        S8("int b[4]; void f(void) { __builtin_constant_p(b + 1); }"),
        S8("void f(int c, int* p, int* q) { __builtin_constant_p(c ? p : q); }"),
        S8("void f(void) { __builtin_constant_p(\"ab\" + 1); }"),
    };
    for (u32 source_index = 0; source_index < BUSTER_ARRAY_LENGTH(sources); source_index += 1)
    {
        TemporalArena temporary = scratch_begin(&arguments->arena, 1);
        CPreprocessResult preprocess = c_ast_test_preprocess(temporary.arena, sources[source_index], C_PREPROCESS_DIALECT_GNU17);
        CAstResult built = c_ast_build(temporary.arena, preprocess, (CAstOptions){0});
        CAstFacts facts = {0};
        if (BUSTER_REQUIRE(arguments, built.complete && c_ast_facts_create(&facts, &built.ast)))
        {
            CParserResult syntax = c_parse_ast(temporary.arena, preprocess);
            syntax.ast = &built.ast;
            syntax.type_interning = false;
            CAnalysisResult plain = c_analyze_semantics_only(temporary.arena, preprocess, syntax);
            syntax.ast_facts = &facts;
            c_test_ast_fact_uncached_set(true);
            CAnalysisResult analysis = c_analyze_semantics_only(temporary.arena, preprocess, syntax);
            c_test_ast_fact_uncached_set(false);
            BUSTER_TEST(arguments, analysis.analysis_complete && analysis.diagnostic_count == 0);
            String8 difference = c_ast_corpus_analyses_differ(temporary.arena, &plain, &analysis);
            difference = difference.length ? difference : c_ast_corpus_model_differ(temporary.arena, &plain, &analysis);
            BUSTER_TEST_RAW(arguments, difference.length == 0, difference);
            BUSTER_TEST(arguments, facts.statistics.recipes && facts.recipe_count && !facts.recipe_failed);
            for (u32 index = 0; index < facts.recipe_count; index += 1)
            {
                CAstTypeRecipe const* recipe = facts.recipes + index;
                BUSTER_TEST(arguments, recipe->form == C_AST_TYPE_RECIPE_BUILTIN ||
                                           (recipe->form == C_AST_TYPE_RECIPE_DERIVATION && recipe->recipe_begin <= index));
                CTypeId const references[] = {recipe->type.element_type, recipe->type.return_type, recipe->type.unqualified_type};
                for (u32 reference = 0; reference < BUSTER_ARRAY_LENGTH(references); reference += 1)
                {
                    u32 row = references[reference].value;
                    BUSTER_TEST(arguments, row == C_ID_UNDERLYING_INVALID || row < recipe->row_begin ||
                                               (u64)recipe->recipe_begin + row - recipe->row_begin < facts.recipe_count);
                }
                if (recipe->type.kind == C_TYPE_ARRAY && recipe->bound.has_inferred_count)
                {
                    BUSTER_TEST(arguments, recipe->bound.inferred_count == (source_index == 3 ? 3u : 4u) && recipe->bound_node == C_AST_NODE_INVALID);
                }
            }
            BUSTER_TEST(arguments, facts.statistics.gaps[C_AST_FACT_GAP_APPENDS] == 0 && facts.statistics.stale == 0);
        }
        c_ast_facts_release(&facts);
        c_ast_release(&built.ast);
        c_test_scratch_end(temporary);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult c_ast_test_retained_entrypoints(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    TemporalArena temporary = scratch_begin(&arguments->arena, 1);
    CPreprocessResult preprocess = c_ast_test_preprocess(temporary.arena,
        S8("int f(int a) { int values[2] = {a, a + 1}; int b = values[1]; return b; }"), C_PREPROCESS_DIALECT_GNU17);
    CParseResult model = c_parse(temporary.arena, preprocess);
    BUSTER_TEST(arguments, model.analysis_complete && !model.diagnostic_count && !model.lowering_validated);
    CIRLowerResult incomplete = c_lower_to_ir(temporary.arena, S8("model only"), preprocess, model, target_native);
    BUSTER_TEST(arguments, !incomplete.program && incomplete.diagnostic_count == 1);
    CParserResult syntax = c_parse_ast(temporary.arena, preprocess);
    CAnalysisResult analysis = c_analyze_semantics_only(temporary.arena, preprocess, syntax);
    if (BUSTER_REQUIRE(arguments, analysis.analysis_complete && !analysis.diagnostic_count && analysis.ast && analysis.ast_facts))
    {
        BUSTER_TEST(arguments, analysis.lowering_validated && analysis.ast == syntax.ast && analysis.frontend_retained_copy_bytes > syntax.ast_retained_copy_bytes);
        BUSTER_TEST(arguments, !analysis.ast_facts->arena && !analysis.ast_facts->index_arena && analysis.ast_facts->spans && analysis.ast_facts->matches);
        BUSTER_TEST(arguments, analysis.initializer_plan_count != 0);
        u32 type_count = analysis.type_count;
        CIRLowerResult first = c_lower_to_ir(temporary.arena, S8("retained first"), preprocess, analysis, target_native);
        CIRLowerResult second = c_lower_to_ir(temporary.arena, S8("retained second"), preprocess, analysis, target_native);
        BUSTER_TEST(arguments, first.program && second.program && !first.diagnostic_count && !second.diagnostic_count);
        BUSTER_TEST(arguments, first.canonical_ir_certified && second.canonical_ir_certified);
        BUSTER_TEST(arguments, c_ast_test_instruction_total(&first) == c_ast_test_instruction_total(&second) && analysis.type_count == type_count);
    }
    CAstResult tree = c_ast_build(temporary.arena, preprocess, (CAstOptions){0});
    CAstFacts facts = {0};
    if (BUSTER_REQUIRE(arguments, tree.complete && c_ast_facts_create(&facts, &tree.ast)))
    {
        CParserResult borrowed = c_parse_ast_from_tree(temporary.arena, preprocess, &tree.ast, 0);
        borrowed.ast = &tree.ast;
        borrowed.ast_facts = &facts;
        CAnalysisResult analyzed = c_analyze_semantics_only(temporary.arena, preprocess, borrowed);
        BUSTER_TEST(arguments, analyzed.analysis_complete && !analyzed.diagnostic_count && analyzed.frontend_retained_copy_bytes == 0);
        BUSTER_TEST(arguments, analyzed.ast == &tree.ast && analyzed.ast_facts == &facts && facts.arena && facts.index_arena);
        CIRLowerResult lowered = c_lower_to_ir(temporary.arena, S8("borrowed"), preprocess, analyzed, target_native);
        BUSTER_TEST(arguments, lowered.program && !lowered.diagnostic_count && lowered.canonical_ir_certified);
    }
    c_ast_facts_release(&facts);
    c_ast_release(&tree.ast);
    CPreprocessResult sidecar_preprocess = c_ast_test_preprocess(temporary.arena,
        S8("struct S { char c; int x; }; long double wide = 1.25L; "
           "unsigned long offset = __builtin_offsetof(struct S, x); "
           "int f(int n) { int values[2] = {n, n + 1}; "
           "int z = _Generic(values[1], int: values[1], default: 0); "
           "if (((z))) z += (int)sizeof(int (*)(int, double)); "
           "return z + (int)offset + (int)wide; }"), C_PREPROCESS_DIALECT_GNU17);
    CAnalysisResult compact = c_parse_for_lowering(temporary.arena, sidecar_preprocess);
    if (BUSTER_REQUIRE(arguments, compact.analysis_complete && !compact.diagnostic_count && compact.ast && compact.ast_facts))
    {
        CAstFacts const* retained = compact.ast_facts;
        BUSTER_TEST(arguments, compact.lowering_validated && compact.frontend_retained_copy_bytes != 0);
        BUSTER_TEST(arguments, !retained->arena && !retained->index_arena && !retained->recipe_arena && !retained->constants_arena);
        BUSTER_TEST(arguments, compact.ast->parenthesis_count != 0 && retained->recipe_count != 0 && retained->constant_count != 0);
        BUSTER_TEST(arguments, retained->constant_capacity == retained->constant_count);
        BUSTER_TEST(arguments, retained->offsetof_plan_count != 0 && retained->offsetof_plan_capacity == retained->offsetof_plan_count &&
                               retained->offsetof_step_count != 0 && retained->offsetof_step_capacity == retained->offsetof_step_count);
        BUSTER_TEST(arguments, retained->ext80_initializer_count != 0 && retained->ext80_initializer_capacity == retained->ext80_initializer_count);
        bool parameter = false;
        for (u32 row = 0; row < retained->recipe_count; row += 1)
        {
            parameter |= retained->recipes[row].form == C_AST_TYPE_RECIPE_PARAMETER;
        }
        BUSTER_TEST(arguments, parameter);
        u32 type_count = compact.type_count;
        CIRLowerResult first = c_lower_to_ir(temporary.arena, S8("retained sidecars first"), sidecar_preprocess, compact, target_native);
        CIRLowerResult second = c_lower_to_ir(temporary.arena, S8("retained sidecars second"), sidecar_preprocess, compact, target_native);
        BUSTER_TEST(arguments, first.program && second.program && !first.diagnostic_count && !second.diagnostic_count);
        BUSTER_TEST(arguments, first.canonical_ir_certified && second.canonical_ir_certified && compact.type_count == type_count);
        BUSTER_TEST(arguments, c_ast_test_instruction_total(&first) == c_ast_test_instruction_total(&second));
    }
    c_test_scratch_end(temporary);
    return result;
}

// Full type names retain their semantic base and abstract constructor graph,
// including nested function parameters and VLA bound node identity.
BUSTER_GLOBAL_LOCAL UnitTestResult c_ast_test_retained_type_names(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    String8 const sources[] = {
        S8("typedef int T; void f(int n) { (void)sizeof(T); (void)sizeof(int (*)[n]); (void)sizeof(int (*)(int, double)); }"),
        S8("void f(void) { (void)sizeof(int (*(*)(double))[3]); (void)sizeof(_Atomic(int*)); (void)sizeof(int (*)(int[3])); }"),
        S8("struct S { int a; }; void f(int c) { (void)((struct S){0}); (void)(c ? (struct S){1} : (struct S){2}); (void)sizeof(struct { int a; }); }"),
        S8("void f(int n) { (void)sizeof(__typeof__(n)); (void)sizeof(int (*)[n + 1]); }"),
        S8("struct S { char* p; unsigned long n; }; void f(int c) { (void)(c ? ((struct S){.p = \"x\", .n = 1}).p : ((struct S){.p = \"y\", .n = 2}).p); (void)(c ? ((struct S){.p = \"x\", .n = 1}).p : 0); }"),
    };
    for (u32 source_index = 0; source_index < BUSTER_ARRAY_LENGTH(sources); source_index += 1)
    {
        TemporalArena temporary = scratch_begin(&arguments->arena, 1);
        CPreprocessResult preprocess = c_ast_test_preprocess(temporary.arena, sources[source_index], C_PREPROCESS_DIALECT_GNU17);
        CAstResult built = c_ast_build(temporary.arena, preprocess, (CAstOptions){0});
        CAstFacts facts = {0};
        if (BUSTER_REQUIRE(arguments, built.complete && c_ast_facts_create(&facts, &built.ast)))
        {
            CParserResult syntax = c_parse_ast(temporary.arena, preprocess);
            syntax.ast = &built.ast;
            syntax.type_interning = true;
            CAnalysisResult plain = c_analyze_semantics_only(temporary.arena, preprocess, syntax);
            syntax.ast_facts = &facts;
            CAnalysisResult analysis = c_analyze_semantics_only(temporary.arena, preprocess, syntax);
            BUSTER_TEST(arguments, analysis.analysis_complete && analysis.diagnostic_count == 0);
            String8 difference = c_ast_corpus_analyses_differ(temporary.arena, &plain, &analysis);
            difference = difference.length ? difference : c_ast_corpus_model_differ(temporary.arena, &plain, &analysis);
            BUSTER_TEST_RAW(arguments, difference.length == 0, difference);
            BUSTER_TEST(arguments, facts.statistics.type_names && !facts.statistics.missing_type_names && !facts.recipe_failed);
            for (u32 node = 0; node < built.ast.node_count; node += 1)
            {
                if (built.ast.kinds[node] == C_AST_TYPE_NAME)
                {
                    u32 base = c_ast_child_at(&built.ast, node, 0);
                    BUSTER_TEST(arguments, (facts.flags[node] & C_AST_FACT_TYPED) && (facts.flags[base] & C_AST_FACT_TYPED));
                    u32 type = facts.types[node];
                    bool recipe = (facts.flags[node] & C_AST_FACT_RECIPE) != 0;
                    BUSTER_TEST(arguments, recipe ? type < facts.recipe_count && facts.recipes[type].form == C_AST_TYPE_RECIPE_DERIVATION
                                                 : type < analysis.type_count);
                    if (source_index == 1)
                    {
                        BUSTER_TEST(arguments, !(facts.payloads[node] & C_AST_TYPE_NAME_VARIABLY_MODIFIED));
                    }
                }
            }
            for (u32 index = 0; index < facts.recipe_count; index += 1)
            {
                CAstTypeRecipe const* recipe = facts.recipes + index;
                if (recipe->form == C_AST_TYPE_RECIPE_DERIVATION && recipe->type.kind == C_TYPE_ARRAY && recipe->bound.token_count)
                {
                    BUSTER_TEST(arguments, recipe->bound_node != C_AST_NODE_INVALID);
                    if (source_index == 1)
                    {
                        BUSTER_TEST(arguments, recipe->count_is_constant && recipe->resolved_count == 3);
                    }
                }
                if (recipe->form == C_AST_TYPE_RECIPE_DERIVATION && recipe->type.kind == C_TYPE_FUNCTION && recipe->type.parameter_count)
                {
                    u32 first = recipe->parameter_recipe_begin + recipe->type.parameter_start - recipe->parameter_row_begin;
                    BUSTER_TEST(arguments, first + recipe->type.parameter_count <= facts.recipe_count);
                    for (u32 parameter = 0; parameter < recipe->type.parameter_count; parameter += 1)
                    {
                        BUSTER_TEST(arguments, facts.recipes[first + parameter].form == C_AST_TYPE_RECIPE_PARAMETER);
                        CAstTypeRecipe const* row = facts.recipes + first + parameter;
                        if (row->parameter_source_type.value >= row->row_begin)
                        {
                            u32 source = row->recipe_begin + row->parameter_source_type.value - row->row_begin;
                            BUSTER_TEST(arguments, source < facts.recipe_count && facts.recipes[source].form == C_AST_TYPE_RECIPE_DERIVATION);
                        }
                    }
                }
            }
            BUSTER_TEST(arguments, !facts.statistics.stale);
            if (source_index == 2 || source_index == 4)
            {
                for (u32 node = 0; node < built.ast.node_count; node += 1)
                {
                    if (built.ast.kinds[node] == C_AST_CONDITIONAL)
                    {
                        BUSTER_TEST(arguments, facts.flags[node] & C_AST_FACT_TYPED);
                        u32 type = facts.types[node];
                        bool recipe = (facts.flags[node] & C_AST_FACT_RECIPE) != 0;
                        CType const* row = recipe ? type < facts.recipe_count ? &facts.recipes[type].type : 0
                                                  : type < analysis.type_count ? analysis.types + type : 0;
                        BUSTER_TEST(arguments, row && row->kind == (source_index == 4 ? C_TYPE_POINTER : C_TYPE_STRUCT));
                    }
                }
            }
        }
        c_ast_facts_release(&facts);
        c_ast_release(&built.ast);
        c_test_scratch_end(temporary);
    }
    return result;
}

// Type-name source facts preserve alias identity and base/constructor
// separation. Unevaluated compound literals still own completed array bounds.
BUSTER_GLOBAL_LOCAL UnitTestResult c_ast_test_retained_type_projection(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    TemporalArena temporary = scratch_begin(&arguments->arena, 1);
    CPreprocessResult preprocess = c_ast_test_preprocess(temporary.arena,
        S8("typedef _Atomic(int) Atomic __attribute__((aligned(32)));"
           "int f(void){return sizeof(void(*(*)(void*,const char*))(void)) + sizeof((int[]){1,2,3})"
           " + _Alignof(_Atomic Atomic) + sizeof((char*[]){\"x\"}) + sizeof(__typeof__((0, (void(*)(void))0)));}"), C_PREPROCESS_DIALECT_GNU17);
    CAstResult built = c_ast_build(temporary.arena, preprocess, (CAstOptions){0});
    CAstFacts facts = {0};
    if (BUSTER_REQUIRE(arguments, built.complete && c_ast_facts_create(&facts, &built.ast)))
    {
        CParserResult syntax = c_parse_ast(temporary.arena, preprocess);
        syntax.ast = &built.ast;
        CAnalysisResult plain = c_analyze_semantics_only(temporary.arena, preprocess, syntax);
        syntax.ast_facts = &facts;
        CAnalysisResult analysis = c_analyze_semantics_only(temporary.arena, preprocess, syntax);
        BUSTER_TEST(arguments, analysis.analysis_complete && !analysis.diagnostic_count);
        String8 difference = c_ast_corpus_analyses_differ(temporary.arena, &plain, &analysis);
        difference = difference.length ? difference : c_ast_corpus_model_differ(temporary.arena, &plain, &analysis);
        BUSTER_TEST_RAW(arguments, !difference.length, difference);
        u32 aliases = 0;
        u32 arrays = 0;
        u32 void_bases = 0;
        u64 const expected[] = {3, 1};
        for (u32 node = 0; node < built.ast.node_count; node += 1)
        {
            if (built.ast.kinds[node] == C_AST_TYPEDEF_NAME)
            {
                BUSTER_TEST(arguments, (facts.flags[node] & (C_AST_FACT_TYPED | C_AST_FACT_BOUND)) == (C_AST_FACT_TYPED | C_AST_FACT_BOUND));
                BUSTER_TEST(arguments, facts.types[node] < analysis.type_count && analysis.types[facts.types[node]].is_atomic);
                aliases += 1;
            }
            else if (built.ast.kinds[node] == C_AST_COMPOUND_LITERAL)
            {
                u32 index = facts.types[node];
                CAstTypeRecipe const* recipe = (facts.flags[node] & C_AST_FACT_RECIPE) && index < facts.recipe_count ? facts.recipes + index : 0;
                BUSTER_TEST(arguments, recipe && recipe->form == C_AST_TYPE_RECIPE_DERIVATION && recipe->type.kind == C_TYPE_ARRAY);
                BUSTER_TEST(arguments, recipe && recipe->bound.has_inferred_count && arrays < BUSTER_ARRAY_LENGTH(expected) && recipe->bound.inferred_count == expected[arrays]);
                arrays += 1;
            }
            else if (built.ast.kinds[node] == C_AST_TYPE_NAME && c_ast_child_count(&built.ast, node) == 2)
            {
                u32 base = c_ast_child_at(&built.ast, node, 0);
                u32 index = facts.types[base];
                CType const* type = facts.flags[base] & C_AST_FACT_RECIPE ? index < facts.recipe_count ? &facts.recipes[index].type : 0
                                                                        : index < analysis.type_count ? analysis.types + index : 0;
                void_bases += type && type->kind == C_TYPE_VOID;
            }
        }
        BUSTER_TEST(arguments, aliases == 1 && arrays == BUSTER_ARRAY_LENGTH(expected) && void_bases == 2);
        u32 type_count = analysis.type_count;
        CIRLowerResult lowered = c_lower_to_ir(temporary.arena, S8("retained-type-projection.c"), preprocess, analysis, target_native);
        BUSTER_TEST(arguments, lowered.program && !lowered.diagnostic_count && lowered.canonical_ir_certified && analysis.type_count == type_count);
    }
    c_ast_facts_release(&facts);
    c_ast_release(&built.ast);
    c_test_scratch_end(temporary);
    return result;
}

// File-scope sizeof/typeof operands never enter a body or initializer region.
// Their expression facts still retain the semantic callable return authority.
BUSTER_GLOBAL_LOCAL UnitTestResult c_ast_test_unevaluated_operand_facts(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    TemporalArena temporary = scratch_begin(&arguments->arena, 1);
    CPreprocessResult preprocess = c_ast_test_preprocess(temporary.arena,
        S8("long counted_long(void);int values[sizeof(counted_long())];"
           "struct R{long value;};struct R get_record(void);typedef __typeof__(get_record().value) Value;"
           "int main(void){return sizeof(values)!=sizeof(int)*sizeof(long);}"), C_PREPROCESS_DIALECT_GNU17);
    CAstResult built = c_ast_build(temporary.arena, preprocess, (CAstOptions){0});
    CAstFacts facts = {0};
    if (BUSTER_REQUIRE(arguments, built.complete && c_ast_facts_create(&facts, &built.ast)))
    {
        CParserResult syntax = c_parse_ast(temporary.arena, preprocess);
        syntax.ast = &built.ast;
        CAnalysisResult plain = c_analyze_semantics_only(temporary.arena, preprocess, syntax);
        syntax.ast_facts = &facts;
        CAnalysisResult analysis = c_analyze_semantics_only(temporary.arena, preprocess, syntax);
        BUSTER_TEST(arguments, analysis.analysis_complete && !analysis.diagnostic_count);
        String8 difference = c_ast_corpus_analyses_differ(temporary.arena, &plain, &analysis);
        difference = difference.length ? difference : c_ast_corpus_model_differ(temporary.arena, &plain, &analysis);
        BUSTER_TEST_RAW(arguments, !difference.length, difference);
        u32 calls = 0;
        for (u32 node = 0; node < built.ast.node_count; node += 1)
        {
            if (built.ast.kinds[node] == C_AST_CALL)
            {
                BUSTER_TEST(arguments, (facts.flags[node] & (C_AST_FACT_EXPRESSION | C_AST_FACT_TYPED)) == (C_AST_FACT_EXPRESSION | C_AST_FACT_TYPED));
                BUSTER_TEST(arguments, !(facts.flags[node] & C_AST_FACT_RECIPE) && facts.types[node] < analysis.type_count &&
                                       analysis.types[facts.types[node]].kind == (calls ? C_TYPE_STRUCT : C_TYPE_LONG));
                u32 callee = c_ast_child_at(&built.ast, node, 0);
                BUSTER_TEST(arguments, (facts.flags[callee] & C_AST_FACT_BOUND) && facts.payloads[callee] < analysis.entity_count);
                calls += 1;
            }
        }
        BUSTER_TEST(arguments, calls == 2 && !facts.statistics.stale && !facts.recipe_failed);
        CIRLowerResult lowered = c_lower_to_ir(temporary.arena, S8("unevaluated-operands.c"), preprocess, analysis, target_native);
        BUSTER_TEST(arguments, lowered.program && !lowered.diagnostic_count && lowered.canonical_ir_certified);
    }
    c_ast_facts_release(&facts);
    c_ast_release(&built.ast);
    c_test_scratch_end(temporary);
    return result;
}

// Completed indirection facts compose from their children. Query count is
// bounded independently of the grouped function designator's input depth.
BUSTER_GLOBAL_LOCAL UnitTestResult c_ast_test_retained_indirection_work(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    TemporalArena temporary = scratch_begin(&arguments->arena, 1);
    u32 depth = 4096;
    String8 head = S8("int g(void);int f(void){int(*p)(void)=g;return ");
    String8 tail = S8("();}");
    u64 bytes = head.length + (u64)depth * 3 + 1 + tail.length;
    char8* text = arena_allocate(temporary.arena, char8, bytes);
    u64 cursor = 0;
    memcpy(text + cursor, head.pointer, head.length);
    cursor += head.length;
    for (u32 index = 0; index < depth; index += 1)
    {
        text[cursor++] = '(';
        text[cursor++] = '*';
    }
    text[cursor++] = 'p';
    for (u32 index = 0; index < depth; index += 1) text[cursor++] = ')';
    memcpy(text + cursor, tail.pointer, tail.length);
    CPreprocessResult preprocess = c_ast_test_preprocess(temporary.arena, (String8){.pointer = text, .length = bytes}, C_PREPROCESS_DIALECT_GNU17);
    CParseResult analysis = c_parse_for_lowering(temporary.arena, preprocess);
    if (BUSTER_REQUIRE(arguments, !preprocess.diagnostic_count && !analysis.diagnostic_count && analysis.ast && analysis.ast_facts))
    {
        CAst const* ast = analysis.ast;
        CAstFacts const* facts = analysis.ast_facts;
        u32 found = 0;
        for (u32 node = 0; node < ast->node_count; node += 1)
        {
            if (ast->kinds[node] == C_AST_DEREFERENCE)
            {
                found += 1;
                BUSTER_TEST(arguments, (facts->flags[node] & C_AST_FACT_TYPED) && !(facts->flags[node] & C_AST_FACT_RECIPE));
                BUSTER_TEST(arguments, facts->types[node] < analysis.type_count && analysis.types[facts->types[node]].kind == C_TYPE_FUNCTION);
            }
        }
        BUSTER_TEST(arguments, found == depth && facts->statistics.completion_queries < 32);
        BUSTER_TEST(arguments, !facts->statistics.gaps[C_AST_FACT_GAP_INVALID] && !facts->statistics.gaps[C_AST_FACT_GAP_APPENDS] && !facts->statistics.stale);
        CIRLowerResult lowered = c_lower_to_ir(temporary.arena, S8("retained-indirection.c"), preprocess, analysis, target_native);
        BUSTER_TEST(arguments, lowered.program && !lowered.diagnostic_count && lowered.canonical_ir_certified);
    }
    c_test_scratch_end(temporary);
    return result;
}

// #4194: a call following a member or subscript retains the callable's
// return type when a declaration's typeof operand uses that postfix chain.
BUSTER_GLOBAL_LOCAL UnitTestResult c_ast_test_typeof_postfix_calls(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    String8 const sources[] = {
        S8("typedef int(*Unary)(int);struct Holder{Unary callback;};struct Holder*get_holder(void);"
           "typedef __typeof__(get_holder()->callback(0)) Result;"
           "_Static_assert(__builtin_types_compatible_p(Result,int),\"member call return\");int main(void){return 0;}"),
        S8("typedef int(*Unary)(int);Unary*get_table(void);typedef __typeof__(get_table()[0](0)) Result;"
           "_Static_assert(__builtin_types_compatible_p(Result,int),\"indexed call return\");int main(void){return 0;}"),
        S8("struct R{long value;};typedef struct R*(*Callback)(int);struct Holder{Callback callback;};struct Holder*get_holder(void);"
           "typedef __typeof__(get_holder()->callback(0)->value) Result;"
           "_Static_assert(__builtin_types_compatible_p(Result,long),\"continued call return\");int main(void){return 0;}"),
        S8("struct Holder{int callback;};struct Holder*get_holder(void);typedef __typeof__(get_holder()->callback(0)) Result;"
           "_Static_assert(__builtin_types_compatible_p(Result,int),\"noncallable member\");int main(void){return 0;}"),
    };
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(sources); index += 1)
    {
        TemporalArena temporary = scratch_begin(&arguments->arena, 1);
        CPreprocessResult preprocess = c_ast_test_preprocess(temporary.arena, sources[index], C_PREPROCESS_DIALECT_GNU17);
        CParseResult analysis = c_parse_for_lowering(temporary.arena, preprocess);
        BUSTER_TEST(arguments, !preprocess.diagnostic_count && analysis.analysis_complete);
        if (index < 3)
        {
            BUSTER_TEST(arguments, !analysis.diagnostic_count);
            CTypeKind kind = index == 2 ? C_TYPE_LONG : C_TYPE_INT;
            bool alias = false;
            for (u32 entity = 0; entity < analysis.entity_count; entity += 1)
            {
                CEntity const* row = analysis.entities + entity;
                if (row->kind == C_ENTITY_TYPEDEF && string_equal(row->name, S8("Result")))
                {
                    alias = row->type.value < analysis.type_count && analysis.types[row->type.value].kind == kind;
                }
            }
            BUSTER_TEST(arguments, alias);
            CIRLowerResult lowered = c_lower_to_ir(temporary.arena, S8("typeof-postfix-call.c"), preprocess, analysis, target_native);
            BUSTER_TEST(arguments, lowered.program && !lowered.diagnostic_count && lowered.canonical_ir_certified);
        }
        else
        {
            BUSTER_TEST(arguments, analysis.diagnostic_count != 0);
        }
        c_test_scratch_end(temporary);
    }
    return result;
}

// Reused scope bitsets may not turn a file-scope enum in the second comma
// initializer into a prototype-scoped enum. Actual prototypes stay isolated.
BUSTER_GLOBAL_LOCAL UnitTestResult c_ast_test_enum_scope_scratch_reuse(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    String8 const sources[] = {
        S8("unsigned long x=sizeof(enum{A=1}),y=sizeof(enum{B=A+1});int main(void){return A+B-3;}"),
        S8("unsigned long x=sizeof(enum{A=1}),y=sizeof(enum{B=A+1}),z=sizeof(enum{C=B+1});int main(void){return A+B+C-6;}"),
        S8("enum{A=1};unsigned long x=sizeof(int(*)(int[sizeof(enum{B=2})]));int main(void){return A-1;}"),
        S8("enum{A=1};unsigned long x=sizeof(int(*)(int[sizeof(enum{B=2})]));int main(void){return B;}"),
    };
    for (u32 source_index = 0; source_index < BUSTER_ARRAY_LENGTH(sources); source_index += 1)
    {
        TemporalArena temporary = scratch_begin(&arguments->arena, 1);
        CPreprocessResult preprocess = c_ast_test_preprocess(temporary.arena, sources[source_index], C_PREPROCESS_DIALECT_GNU17);
        CAstResult built = c_ast_build(temporary.arena, preprocess, (CAstOptions){0});
        CAstFacts facts = {0};
        if (BUSTER_REQUIRE(arguments, built.complete && c_ast_facts_create(&facts, &built.ast)))
        {
            CParserResult syntax = c_parse_ast(temporary.arena, preprocess);
            syntax.ast = &built.ast;
            syntax.ast_facts = &facts;
            syntax.type_interning = true;
            c_test_enum_scope_dirty_set(true);
            CAnalysisResult analysis = c_analyze_semantics_only(temporary.arena, preprocess, syntax);
            c_test_enum_scope_dirty_set(false);
            bool accepted = source_index != 3;
            BUSTER_TEST(arguments, analysis.analysis_complete && (accepted ? analysis.diagnostic_count == 0 : analysis.diagnostic_count != 0));
            BUSTER_TEST(arguments, analysis.enum_member_count == (source_index == 1 ? 3u : 2u));
            for (u32 index = 0; index < analysis.enum_member_count; index += 1)
            {
                CEnumMember const* member = analysis.enum_members + index;
                bool prototype = source_index >= 2 && index == 1;
                BUSTER_TEST(arguments, member->is_prototype_scope == prototype && member->is_published != prototype);
            }
            if (!accepted)
            {
                u32 undeclared = 0;
                for (u32 index = 0; index < analysis.diagnostic_count; index += 1)
                {
                    undeclared += analysis.diagnostics[index].kind == C_DIAGNOSTIC_UNDECLARED_IDENTIFIER;
                }
                BUSTER_TEST(arguments, undeclared == 1);
            }
        }
        c_ast_facts_release(&facts);
        c_ast_release(&built.ast);
        c_test_scratch_end(temporary);
    }
    return result;
}

UnitTestResult c_ast_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    // The thread's unit registry reserves its own arena at its first unit;
    // reserve it before the first fixture's reading.
    {
        TemporalArena temporary = scratch_begin(&arguments->arena, 1);
        c_ast_test_preprocess(temporary.arena, S8(""), C_PREPROCESS_DIALECT_GNU17);
        c_test_scratch_end(temporary);
    }
    C_TEST_BOUNDED_FIXTURE(arguments, c_ast_test_source_anchors);
    C_TEST_BOUNDED_FIXTURE(arguments, c_ast_test_expressions);
    C_TEST_BOUNDED_FIXTURE(arguments, c_ast_test_declarations);
    C_TEST_BOUNDED_FIXTURE(arguments, c_ast_test_dialects);
    C_TEST_BOUNDED_FIXTURE(arguments, c_ast_test_failures);
    C_TEST_BOUNDED_FIXTURE(arguments, c_ast_test_tables);
    C_TEST_BOUNDED_FIXTURE(arguments, c_ast_test_navigation);
    C_TEST_BOUNDED_FIXTURE(arguments, c_ast_test_spans);
    C_TEST_BOUNDED_FIXTURE(arguments, c_ast_test_validator);
    C_TEST_BOUNDED_FIXTURE(arguments, c_ast_test_attribute_slots);
    C_TEST_BOUNDED_FIXTURE(arguments, c_ast_test_vendor_attribute_arguments);
    C_TEST_BOUNDED_FIXTURE(arguments, c_ast_test_options_and_statistics);
    C_TEST_BOUNDED_FIXTURE(arguments, c_ast_test_columns);
    C_TEST_BOUNDED_FIXTURE(arguments, c_ast_test_parentheses);
    C_TEST_BOUNDED_FIXTURE(arguments, c_ast_test_owned_delimiters);
    C_TEST_BOUNDED_FIXTURE(arguments, c_ast_test_oracle);
    C_TEST_BOUNDED_FIXTURE(arguments, c_ast_test_uninterned);
    C_TEST_BOUNDED_FIXTURE(arguments, c_ast_test_types);
    C_TEST_BOUNDED_FIXTURE(arguments, c_ast_test_type_lookups);
    C_TEST_BOUNDED_FIXTURE(arguments, c_ast_test_split);
    C_TEST_BOUNDED_FIXTURE(arguments, c_ast_test_statements);
    C_TEST_BOUNDED_FIXTURE(arguments, c_ast_test_statement_nodes);
    C_TEST_BOUNDED_FIXTURE(arguments, c_ast_test_assembly_nodes);
    C_TEST_BOUNDED_FIXTURE(arguments, c_ast_test_specifiers);
    C_TEST_BOUNDED_FIXTURE(arguments, c_ast_test_type_names);
    C_TEST_BOUNDED_FIXTURE(arguments, c_ast_test_declaration_hints);
    C_TEST_BOUNDED_FIXTURE(arguments, c_ast_test_members);
    C_TEST_BOUNDED_FIXTURE(arguments, c_ast_test_initializer_plans);
    C_TEST_BOUNDED_FIXTURE(arguments, c_ast_test_initializer_lower_reuse);
    C_TEST_BOUNDED_FIXTURE(arguments, c_ast_test_initializer_promoted_depth);
    C_TEST_BOUNDED_FIXTURE(arguments, c_ast_test_static_assert_authority);
    C_TEST_BOUNDED_FIXTURE(arguments, c_ast_test_unit_owner_plans);
    C_TEST_BOUNDED_FIXTURE(arguments, c_ast_test_vendor_budget_nodes);
    C_TEST_BOUNDED_FIXTURE(arguments, c_ast_test_symbol_bindings);
    C_TEST_BOUNDED_FIXTURE(arguments, c_ast_test_static_plan_object_contract);
    C_TEST_BOUNDED_FIXTURE(arguments, c_ast_test_declaration_owners);
    C_TEST_BOUNDED_FIXTURE(arguments, c_ast_test_retained_facts);
    C_TEST_BOUNDED_FIXTURE(arguments, c_ast_test_call_builtin_facts);
    C_TEST_BOUNDED_FIXTURE(arguments, c_ast_test_member_builtin_facts);
    C_TEST_BOUNDED_FIXTURE(arguments, c_ast_test_offsetof_facts);
    C_TEST_BOUNDED_FIXTURE(arguments, c_ast_test_retained_type_recipes);
    C_TEST_BOUNDED_FIXTURE(arguments, c_ast_test_constant_facts);
    C_TEST_BOUNDED_FIXTURE(arguments, c_ast_test_bound_owners);
    C_TEST_BOUNDED_FIXTURE(arguments, c_ast_test_constant_object_context);
    C_TEST_BOUNDED_FIXTURE(arguments, c_ast_test_constant_node_tasks);
    C_TEST_BOUNDED_FIXTURE(arguments, c_ast_test_retained_entrypoints);
    C_TEST_BOUNDED_FIXTURE(arguments, c_ast_test_retained_type_names);
    C_TEST_BOUNDED_FIXTURE(arguments, c_ast_test_retained_type_projection);
    C_TEST_BOUNDED_FIXTURE(arguments, c_ast_test_unevaluated_operand_facts);
    C_TEST_BOUNDED_FIXTURE(arguments, c_ast_test_retained_indirection_work);
    C_TEST_BOUNDED_FIXTURE(arguments, c_ast_test_typeof_postfix_calls);
    C_TEST_BOUNDED_FIXTURE(arguments, c_ast_test_enum_scope_scratch_reuse);
#if !BUSTER_ANDROID && !BUSTER_IOS
    C_TEST_BOUNDED_FIXTURE(arguments, c_ast_test_fixture_sweep);
    C_TEST_BOUNDED_FIXTURE(arguments, c_ast_test_corpus);
    C_TEST_BOUNDED_FIXTURE(arguments, c_ast_test_sizeof_handoff);
#endif
    C_TEST_BOUNDED_FIXTURE(arguments, c_ast_test_truncations);
    C_TEST_BOUNDED_FIXTURE(arguments, c_ast_test_deep);
    return result;
}
#endif
