// The implicit postorder syntax tree (c_ast.h, GitHub #3102): precedence and
// associativity, declarators, typedef shadowing, labels, tags and
// initializers, function definitions, and the structural invariants every
// build must keep. Each case builds the same source with every refill batch
// and every layout and requires one tree: refill-boundary invariance proves no
// rule reads past C_AST_LOOKAHEAD, and layout invariance proves the accessors
// answer the same in IMPLICIT, HYBRID and EXPLICIT. Deep nesting must succeed
// without growing the C stack, and truncated input must fail with one
// diagnostic and no partial tree.
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
#include <buster/lib/compiler/frontend/c/c_ast.h>
#include <buster/lib/string.h>
#include <buster/lib/file.h>

BUSTER_GLOBAL_LOCAL u32 const c_ast_test_batches[] = {0, 1, 2, 3, 7, 64};
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

// The two trees are one tree: the same kinds, extents, anchors and payloads,
// where a LIST node's payload is compared through its list count (HYBRID
// replaces it with a slice offset), and the same children from every
// accessor.
BUSTER_GLOBAL_LOCAL bool c_ast_test_same_tree(CAst const* left, CAst const* right, bool compare_children)
{
    bool same = left->node_count == right->node_count && left->root == right->root;
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
                                               (CAstOptions){.layout = c_ast_test_layouts[layout], .refill_batch = c_ast_test_batches[batch]});
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
            }
        }
    }
    scratch_end(temporary);
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
        }
    }
    scratch_end(temporary);
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
            }
            scratch_end(temporary);
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
                }
            }
        }
        scratch_end(temporary);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL CAst c_ast_test_copy(Arena* arena, CAst const* ast)
{
    CAst copy = *ast;
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
    }
    scratch_end(temporary);
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
    }
    scratch_end(temporary);
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
        BUSTER_TEST(arguments, statistics.retained_bytes == (u64)implicit_result.ast.node_count * 13);
        BUSTER_TEST(arguments, statistics.sealed_copy_bytes == statistics.retained_bytes);
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
        // The ring window refills in batches.
        CAstResult batched = c_ast_build(temporary.arena, preprocess, (CAstOptions){.refill_batch = 5});
        BUSTER_TEST(arguments, batched.complete && batched.statistics.refills * 5 >= preprocess.token_count);
        BUSTER_TEST(arguments, c_ast_test_same_tree(&implicit_result.ast, &batched.ast, false));
        // A very large batch is the whole stream in one refill.
        CAstResult whole = c_ast_build(temporary.arena, preprocess, (CAstOptions){.refill_batch = 1u << 20});
        BUSTER_TEST(arguments, whole.complete && whole.statistics.refills == 1);
        // A supplied phase arena returns to its entry position.
        Arena* phase = arena_create((ArenaCreation){.reserved_size = BUSTER_GB(1)});
        if (BUSTER_REQUIRE(arguments, phase != 0))
        {
            u64 position = phase->position;
            CAstResult shared = c_ast_build(temporary.arena, preprocess, (CAstOptions){.phase_arena = phase});
            BUSTER_TEST(arguments, shared.complete && phase->position == position && shared.statistics.transient_high_water > 0);
            BUSTER_TEST(arguments, c_ast_test_same_tree(&implicit_result.ast, &shared.ast, false));
            CAstResult failed = c_ast_build(temporary.arena, c_ast_test_preprocess(temporary.arena, S8("int x"), C_PREPROCESS_DIALECT_GNU17),
                                            (CAstOptions){.phase_arena = phase});
            BUSTER_TEST(arguments, !failed.complete && failed.diagnostic_count == 1 && phase->position == position);
            arena_destroy(phase, 1);
        }
    }
    scratch_end(temporary);
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
            BUSTER_TEST(arguments, reference.complete && other.complete);
            if (reference.complete && other.complete)
            {
                BUSTER_TEST(arguments, c_ast_validate(&other.ast) == C_AST_NODE_INVALID);
                BUSTER_STRING_TEST(arguments, c_ast_dump(temporary.arena, &reference.ast, preprocess, reference.ast.root),
                                   c_ast_dump(temporary.arena, &other.ast, changed, other.ast.root));
            }
        }
        scratch_end(temporary);
    }
    return result;
}

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
                CParserResult syntax = c_parse_ast(temporary.arena, preprocess);
                CAnalysisResult analysis = c_analyze_semantics_only(temporary.arena, preprocess, syntax);
                CAstResult built = c_ast_build(temporary.arena, preprocess, (CAstOptions){0});
                if (!syntax.diagnostic_count && !analysis.diagnostic_count && analysis.analysis_complete)
                {
                    BUSTER_TEST_RAW(arguments, built.complete, c_ast_test_fixtures[fixture]);
                    BUSTER_TEST_RAW(arguments, !built.complete || c_ast_validate(&built.ast) == C_AST_NODE_INVALID, c_ast_test_fixtures[fixture]);
                    built_count += built.complete;
                }
            }
        }
        scratch_end(temporary);
    }
    BUSTER_TEST(arguments, built_count + 8 >= BUSTER_ARRAY_LENGTH(c_ast_test_fixtures));
    return result;
}

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

UnitTestResult c_ast_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    BUSTER_TEST_FIXTURE(arguments, c_ast_test_expressions);
    BUSTER_TEST_FIXTURE(arguments, c_ast_test_declarations);
    BUSTER_TEST_FIXTURE(arguments, c_ast_test_dialects);
    BUSTER_TEST_FIXTURE(arguments, c_ast_test_failures);
    BUSTER_TEST_FIXTURE(arguments, c_ast_test_tables);
    BUSTER_TEST_FIXTURE(arguments, c_ast_test_navigation);
    BUSTER_TEST_FIXTURE(arguments, c_ast_test_validator);
    BUSTER_TEST_FIXTURE(arguments, c_ast_test_options_and_statistics);
    BUSTER_TEST_FIXTURE(arguments, c_ast_test_oracle);
    BUSTER_TEST_FIXTURE(arguments, c_ast_test_uninterned);
    BUSTER_TEST_FIXTURE(arguments, c_ast_test_fixture_sweep);
    BUSTER_TEST_FIXTURE(arguments, c_ast_test_truncations);
    BUSTER_TEST_FIXTURE(arguments, c_ast_test_deep);
    return result;
}
#endif
