from pathlib import Path


def replace_once(path: Path, old: str, new: str) -> None:
    text = path.read_text(encoding="utf-8")
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one literal match, found {count}")
    path.write_text(text.replace(old, new, 1), encoding="utf-8")


internal = Path("src/buster/lib/compiler/frontend/c/c_internal.h")
parse = Path("src/buster/lib/compiler/frontend/c/c_parse.c")
gen = Path("src/buster/lib/compiler/frontend/c/c_gen.c")
c_test = Path("src/buster/tests/compiler/frontend/c/c_test.c")
driver_test = Path("src/buster/tests/compiler/driver/driver_fast_test.c")

header_marker = """BUSTER_C_EXTERN void c_parse_static_assert_check(CTypeParseMachine* machine, Arena* arena,
                                                  CPreprocessResult preprocess, CParseResult* result,
                                                  CDeclaration declaration, CScopeId scope);
"""
replace_once(
    internal,
    header_marker,
    header_marker + """BUSTER_C_EXTERN String8 c_parse_static_assert_diagnostic_message(Arena* arena, CPreprocessResult preprocess,
                                                                  CDeclaration declaration, CDiagnosticKind kind);
""",
)

definition_marker = """BUSTER_C_SHARED void c_parse_static_assert_check(CTypeParseMachine* machine, Arena* arena, CPreprocessResult preprocess, CParseResult* result,
                                                     CDeclaration declaration, CScopeId scope)
"""
formatter = """BUSTER_C_SHARED String8 c_parse_static_assert_diagnostic_message(Arena* arena, CPreprocessResult preprocess,
                                                                  CDeclaration declaration, CDiagnosticKind kind)
{
    u32 expression_start = 0;
    u32 expression_end = 0;
    bool has_expression = c_parse_static_assert_expression_range(preprocess, declaration, &expression_start, &expression_end);
    if (kind == C_DIAGNOSTIC_STATIC_ASSERT_NOT_CONSTANT)
    {
        String8 expression = has_expression ? c_parse_token_range_text(arena, preprocess, expression_start, expression_end) : (String8){0};
        return expression.length
                   ? string_format(arena, S8("static assertion expression is not an integer constant expression: {S8}"), expression)
                   : S8("static assertion expression is not an integer constant expression");
    }

    u32 declaration_end = declaration.token_start <= preprocess.token_count &&
                                  declaration.token_count <= preprocess.token_count - declaration.token_start
                              ? declaration.token_start + declaration.token_count
                              : preprocess.token_count;
    String8 message = {0};
    if (has_expression && expression_end + 1 < declaration_end &&
        c_token_is_punctuator(&preprocess.tokens[expression_end], C_PUNCTUATOR_COMMA) &&
        preprocess.tokens[expression_end + 1].kind == C_TOKEN_STRING_LITERAL)
    {
        message = c_token_spelling(preprocess.spelling_base, preprocess.tokens[expression_end + 1]);
    }
    return message.length ? string_format(arena, S8("static assertion failed: {S8}"), message) : S8("static assertion failed");
}

"""
replace_once(parse, definition_marker, formatter + definition_marker)

parser_old = """        if (!value.valid)
        {
            c_parse_diagnostic(result, assertion.location, C_DIAGNOSTIC_STATIC_ASSERT_NOT_CONSTANT,
                               S8("static assertion expression is not an integer constant expression"));
        }
        else if (value.is_float || !c_parse_constant_truth(value))
        {
            c_parse_diagnostic(result, assertion.location, C_DIAGNOSTIC_STATIC_ASSERT_FAILED,
                               S8("static assertion expression is not a true integer constant expression"));
        }
    }
    BUSTER_UNUSED(arena);
"""
parser_new = """        if (!value.valid)
        {
            c_parse_diagnostic(result, assertion.location, C_DIAGNOSTIC_STATIC_ASSERT_NOT_CONSTANT,
                               c_parse_static_assert_diagnostic_message(arena, preprocess, declaration,
                                                                        C_DIAGNOSTIC_STATIC_ASSERT_NOT_CONSTANT));
        }
        else if (value.is_float || !c_parse_constant_truth(value))
        {
            c_parse_diagnostic(result, assertion.location, C_DIAGNOSTIC_STATIC_ASSERT_FAILED,
                               c_parse_static_assert_diagnostic_message(arena, preprocess, declaration,
                                                                        C_DIAGNOSTIC_STATIC_ASSERT_FAILED));
        }
    }
"""
replace_once(parse, parser_old, parser_new)

body_old = """                u32 expression_start = 0;
                u32 expression_end = 0;
                CIrConstantValue assertion = {0};
                if (!c_ir_static_assert_expression_range(builder, index, assertion_end + 1, &expression_start, &expression_end) ||
                    !c_ir_constant_evaluate(builder, expression_start, expression_end, &assertion) ||
                    assertion.kind != C_IR_CONSTANT_INTEGER || !c_ir_constant_truth(builder, &assertion))
                {
                    builder->failure_message = S8("static assertion expression is not a true integer constant expression");
                    return false;
                }
"""
body_new = """                u32 expression_start = 0;
                u32 expression_end = 0;
                CIrConstantValue assertion = {0};
                bool expression_valid =
                    c_ir_static_assert_expression_range(builder, index, assertion_end + 1, &expression_start, &expression_end);
                bool constant = expression_valid && c_ir_constant_evaluate(builder, expression_start, expression_end, &assertion);
                CDeclaration assertion_declaration = {
                    .token_start = index,
                    .token_count = assertion_end + 1 - index,
                    .location = c_ir_token_location(builder, first),
                };
                if (!expression_valid || !constant)
                {
                    builder->failure_message = c_parse_static_assert_diagnostic_message(
                        builder->arena, builder->preprocess, assertion_declaration, C_DIAGNOSTIC_STATIC_ASSERT_NOT_CONSTANT);
                    builder->failure_kind_plus_one = C_DIAGNOSTIC_STATIC_ASSERT_NOT_CONSTANT + 1;
                    return false;
                }
                if (assertion.kind != C_IR_CONSTANT_INTEGER || !c_ir_constant_truth(builder, &assertion))
                {
                    builder->failure_message = c_parse_static_assert_diagnostic_message(
                        builder->arena, builder->preprocess, assertion_declaration, C_DIAGNOSTIC_STATIC_ASSERT_FAILED);
                    builder->failure_kind_plus_one = C_DIAGNOSTIC_STATIC_ASSERT_FAILED + 1;
                    return false;
                }
"""
replace_once(gen, body_old, body_new)

file_not_constant_old = """            result.diagnostics[result.diagnostic_count++] = (CDiagnostic){
                .message = S8("static assertion expression is not an integer constant expression"),
                .location = declaration.location,
                .kind = C_DIAGNOSTIC_STATIC_ASSERT_NOT_CONSTANT,
            };
"""
file_not_constant_new = """            result.diagnostics[result.diagnostic_count++] = (CDiagnostic){
                .message = c_parse_static_assert_diagnostic_message(arena, preprocess, declaration,
                                                                    C_DIAGNOSTIC_STATIC_ASSERT_NOT_CONSTANT),
                .location = declaration.location,
                .kind = C_DIAGNOSTIC_STATIC_ASSERT_NOT_CONSTANT,
            };
"""
replace_once(gen, file_not_constant_old, file_not_constant_new)

file_failed_old = """            result.diagnostics[result.diagnostic_count++] = (CDiagnostic){
                .message = S8("static assertion expression is not a true integer constant expression"),
                .location = declaration.location,
                .kind = C_DIAGNOSTIC_STATIC_ASSERT_FAILED,
            };
"""
file_failed_new = """            result.diagnostics[result.diagnostic_count++] = (CDiagnostic){
                .message = c_parse_static_assert_diagnostic_message(arena, preprocess, declaration,
                                                                    C_DIAGNOSTIC_STATIC_ASSERT_FAILED),
                .location = declaration.location,
                .kind = C_DIAGNOSTIC_STATIC_ASSERT_FAILED,
            };
"""
replace_once(gen, file_failed_old, file_failed_new)

replace_once(
    c_test,
    """        BUSTER_TEST(arguments, deferred_assert_false_ir.diagnostics[0].kind == C_DIAGNOSTIC_STATIC_ASSERT_FAILED);
""",
    """        BUSTER_TEST(arguments, deferred_assert_false_ir.diagnostics[0].kind == C_DIAGNOSTIC_STATIC_ASSERT_FAILED);
        BUSTER_STRING_TEST(arguments, deferred_assert_false_ir.diagnostics[0].message,
                           S8("static assertion failed: \\\"false inferred array size\\\""));
""",
)
replace_once(
    c_test,
    """        BUSTER_TEST(arguments, narrowing_false_ir.diagnostics[0].kind == C_DIAGNOSTIC_STATIC_ASSERT_FAILED);
""",
    """        BUSTER_TEST(arguments, narrowing_false_ir.diagnostics[0].kind == C_DIAGNOSTIC_STATIC_ASSERT_FAILED);
        BUSTER_STRING_TEST(arguments, narrowing_false_ir.diagnostics[0].message,
                           S8("static assertion failed: \\\"narrowing cast becomes zero\\\""));
""",
)
replace_once(
    c_test,
    """        BUSTER_TEST(arguments, deferred_assert_nonconstant_ir.diagnostics[0].kind == C_DIAGNOSTIC_STATIC_ASSERT_NOT_CONSTANT);
""",
    """        BUSTER_TEST(arguments, deferred_assert_nonconstant_ir.diagnostics[0].kind == C_DIAGNOSTIC_STATIC_ASSERT_NOT_CONSTANT);
        BUSTER_STRING_TEST(arguments, deferred_assert_nonconstant_ir.diagnostics[0].message,
                           S8("static assertion expression is not an integer constant expression: sizeof(nonconstant_assert_array) == nonconstant_assert_value()"));
""",
)

replace_once(
    c_test,
    """// keep their source adjacency; a comment, line break or macro boundary reads
// as one space. A `_Generic` assertion is deferred and its diagnostic, which
// quotes nothing, is pinned unchanged.
""",
    """// keep their source adjacency; a comment, line break or macro boundary reads
// as one space. Deferred assertions use the same source quote after semantic
// evaluation resolves their controlling expression.
""",
)
replace_once(
    c_test,
    """         S8("static assertion expression is not an integer constant expression")},
    };
""",
    """         S8("static assertion expression is not an integer constant expression: _Generic(0, int: pair(1, 2), default: 0)")},
    };
""",
)

new_test = r'''BUSTER_GLOBAL_LOCAL UnitTestResult c_test_deferred_assert_messages(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    struct
    {
        String8 source;
        String8 message;
    } cases[] = {
        {S8("enum { FILE_ENUM_FALSE = 0 }; _Static_assert(FILE_ENUM_FALSE, \"enum message\");\n"),
         S8("static assertion failed: \"enum message\"")},
        {S8("int local_enum_assert(void) { enum { LOCAL_ENUM_FALSE = 0 }; _Static_assert(LOCAL_ENUM_FALSE, \"local enum message\"); return 0; }\n"),
         S8("static assertion failed: \"local enum message\"")},
        {S8("_Static_assert(_Generic(0, int: 0, default: 1), \"generic message\");\n"),
         S8("static assertion failed: \"generic message\"")},
        {S8("struct OffsetMessage { char first; int second; }; _Static_assert(__builtin_offsetof(struct OffsetMessage, second) == 0, \"offsetof message\");\n"),
         S8("static assertion failed: \"offsetof message\"")},
        {S8("int local_sizeof_assert(void) { char value; _Static_assert(sizeof value == 2, \"sizeof local message\"); return 0; }\n"),
         S8("static assertion failed: \"sizeof local message\"")},
        {S8("_Static_assert(_Generic(0, int: 0, default: 1));\n"), S8("static assertion failed")},
    };
    for (u32 case_index = 0; case_index < BUSTER_ARRAY_LENGTH(cases); case_index += 1)
    {
        for (u32 path = 0; path < 2; path += 1)
        {
            TemporalArena temporary = scratch_begin(0, 0);
            CPreprocessResult tokens = c_preprocess(temporary.arena, cases[case_index].source,
                                                    (CPreprocessOptions){
                                                        .target = target_native,
                                                        .data_layout = target_data_layout(target_native),
                                                    });
            CIRLowerResult diagnostics = {0};
            BUSTER_TEST(arguments, tokens.diagnostic_count == 0);
            if (path == 0)
            {
                CParseResult parsed = c_parse(temporary.arena, tokens);
                BUSTER_TEST(arguments, parsed.diagnostic_count == 0);
                diagnostics = c_lower_to_ir(temporary.arena, S8("deferred-static-assert-message.c"), tokens, parsed, target_native);
            }
            else
            {
                CParserResult syntax = c_parse_ast(temporary.arena, tokens);
                BUSTER_TEST(arguments, syntax.diagnostic_count == 0);
                diagnostics = c_analyze(temporary.arena, S8("deferred-static-assert-message.c"), tokens, syntax, target_native);
            }
            BUSTER_TEST_RAW(arguments, diagnostics.diagnostic_count == 1, cases[case_index].source);
            if (diagnostics.diagnostic_count == 1)
            {
                BUSTER_TEST(arguments, diagnostics.diagnostics[0].kind == C_DIAGNOSTIC_STATIC_ASSERT_FAILED);
                BUSTER_STRING_TEST(arguments, diagnostics.diagnostics[0].message, cases[case_index].message);
            }
            scratch_end(temporary);
        }
    }
    return result;
}

'''
marker = "BUSTER_GLOBAL_LOCAL UnitTestResult c_test_static_assert_nonconstant_quote(UnitTestArguments* arguments)\n"
text = c_test.read_text(encoding="utf-8")
if text.count(marker) != 1:
    raise SystemExit(f"{c_test}: static quote test marker count {text.count(marker)}")
c_test.write_text(text.replace(marker, new_test + marker, 1), encoding="utf-8")

replace_once(
    c_test,
    """    BUSTER_TEST_FIXTURE(arguments, c_test_deferred_assert_nonconstant);

    BUSTER_TEST_FIXTURE(arguments, c_test_static_assert_nonconstant_quote);
""",
    """    BUSTER_TEST_FIXTURE(arguments, c_test_deferred_assert_nonconstant);

    BUSTER_TEST_FIXTURE(arguments, c_test_deferred_assert_messages);

    BUSTER_TEST_FIXTURE(arguments, c_test_static_assert_nonconstant_quote);
""",
)

replace_once(
    driver_test,
    """\"), false, S8("static assertion expression is not a true integer constant expression")},
""",
    """\"), false, S8("static assertion failed: \\\"false\\\"")},
""",
)
