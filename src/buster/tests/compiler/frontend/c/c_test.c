#include <buster/tests/compiler/frontend/c/c_test.h>

BUSTER_GLOBAL_LOCAL void c_test_token(UnitTestArguments* arguments, UnitTestResult* outer_result, CLexResult lex, u64 index, CTokenKind kind, String8 spelling)
{
    UnitTestResult result = {0};
    BUSTER_UNUSED(arguments);
    BUSTER_TEST(arguments, index < lex.token_count);
    if (index < lex.token_count)
    {
        BUSTER_TEST(arguments, lex.tokens[index].kind == kind);
        BUSTER_STRING_TEST(arguments, lex.tokens[index].spelling, spelling);
    }
    outer_result->test_count += result.test_count;
    outer_result->succeeded_test_count += result.succeeded_test_count;
}

BUSTER_GLOBAL_LOCAL void c_test_preprocessed_token(UnitTestArguments* arguments, UnitTestResult* outer_result, CPreprocessResult preprocess, u64 index,
                                                   CTokenKind kind, String8 spelling)
{
    UnitTestResult result = {0};
    BUSTER_UNUSED(arguments);
    BUSTER_TEST(arguments, index < preprocess.token_count);
    if (index < preprocess.token_count)
    {
        BUSTER_TEST(arguments, preprocess.tokens[index].kind == kind);
        BUSTER_STRING_TEST(arguments, preprocess.tokens[index].spelling, spelling);
    }
    outer_result->test_count += result.test_count;
    outer_result->succeeded_test_count += result.succeeded_test_count;
}

BUSTER_TEST_F_DECL UnitTestResult c_frontend_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    CLexResult basic = c_lex(arguments->arena, S8("int ma\\\r\nin(void) // comment\r\n"
                                                  "{ return 0; }\r\n"));
    BUSTER_TEST(arguments, basic.diagnostic_count == 0);
    BUSTER_TEST(arguments, basic.token_count == 13);
    c_test_token(arguments, &result, basic, 0, C_TOKEN_IDENTIFIER, S8("int"));
    c_test_token(arguments, &result, basic, 1, C_TOKEN_IDENTIFIER, S8("main"));
    c_test_token(arguments, &result, basic, 2, C_TOKEN_PUNCTUATOR, S8("("));
    c_test_token(arguments, &result, basic, 3, C_TOKEN_IDENTIFIER, S8("void"));
    c_test_token(arguments, &result, basic, 4, C_TOKEN_PUNCTUATOR, S8(")"));
    if (basic.token_count >= 5)
    {
        BUSTER_TEST(arguments, basic.tokens[1].location.line == 1);
        BUSTER_TEST(arguments, basic.tokens[4].location.line == 2);
    }

    CLexResult tokens = c_lex(arguments->arena, S8("u8\"x\" L'a' .5e+2 0x1p-3 "
                                                   "<<= >>= ... -> ++ -- <% %> %: %:%: $gnu @ \\t\n"));
    BUSTER_TEST(arguments, tokens.diagnostic_count == 0);
    c_test_token(arguments, &result, tokens, 0, C_TOKEN_STRING_LITERAL, S8("u8\"x\""));
    c_test_token(arguments, &result, tokens, 1, C_TOKEN_CHARACTER_LITERAL, S8("L'a'"));
    c_test_token(arguments, &result, tokens, 2, C_TOKEN_PREPROCESSING_NUMBER, S8(".5e+2"));
    c_test_token(arguments, &result, tokens, 3, C_TOKEN_PREPROCESSING_NUMBER, S8("0x1p-3"));
    c_test_token(arguments, &result, tokens, 4, C_TOKEN_PUNCTUATOR, S8("<<="));
    c_test_token(arguments, &result, tokens, 13, C_TOKEN_PUNCTUATOR, S8("%:%:"));
    c_test_token(arguments, &result, tokens, 14, C_TOKEN_IDENTIFIER, S8("$gnu"));
    c_test_token(arguments, &result, tokens, 15, C_TOKEN_PUNCTUATOR, S8("@"));
    c_test_token(arguments, &result, tokens, 16, C_TOKEN_PUNCTUATOR, S8("\\"));

    CLexResult comments = c_lex(arguments->arena, S8("a/* first\nsecond */b\n"));
    BUSTER_TEST(arguments, comments.diagnostic_count == 0);
    BUSTER_TEST(arguments, comments.token_count == 5);
    c_test_token(arguments, &result, comments, 0, C_TOKEN_IDENTIFIER, S8("a"));
    c_test_token(arguments, &result, comments, 1, C_TOKEN_NEWLINE, S8("\n"));
    c_test_token(arguments, &result, comments, 2, C_TOKEN_IDENTIFIER, S8("b"));

    CLexResult invalid = c_lex(arguments->arena, S8("\"unterminated\n/* open"));
    BUSTER_TEST(arguments, invalid.diagnostic_count == 2);
    if (invalid.diagnostic_count == 2)
    {
        BUSTER_TEST(arguments, invalid.diagnostics[0].kind == C_DIAGNOSTIC_UNTERMINATED_STRING_LITERAL);
        BUSTER_TEST(arguments, invalid.diagnostics[1].kind == C_DIAGNOSTIC_UNTERMINATED_BLOCK_COMMENT);
    }

    CPreprocessResult preprocess = c_preprocess(arguments->arena,
                                                S8("#define ANSWER 40\n"
                                                   "#define TWO 2\n"
                                                   "#define NESTED ANSWER\n"
                                                   "NESTED + TWO\n"
                                                   "#undef TWO\n"
                                                   "TWO\n"),
                                                (CPreprocessOptions){0});
    BUSTER_TEST(arguments, preprocess.diagnostic_count == 0);
    BUSTER_TEST(arguments, preprocess.token_count == 5);
    c_test_preprocessed_token(arguments, &result, preprocess, 0, C_TOKEN_PREPROCESSING_NUMBER, S8("40"));
    c_test_preprocessed_token(arguments, &result, preprocess, 1, C_TOKEN_PUNCTUATOR, S8("+"));
    c_test_preprocessed_token(arguments, &result, preprocess, 2, C_TOKEN_PREPROCESSING_NUMBER, S8("2"));
    c_test_preprocessed_token(arguments, &result, preprocess, 3, C_TOKEN_IDENTIFIER, S8("TWO"));

    CPreprocessorDefinition command_definition = {
        .name = S8("COMMAND_VALUE"),
        .value = S8("9"),
    };
    CPreprocessResult command_preprocess = c_preprocess(arguments->arena, S8("COMMAND_VALUE\n"),
                                                        (CPreprocessOptions){
                                                            .definitions = &command_definition,
                                                            .definition_count = 1,
                                                        });
    BUSTER_TEST(arguments, command_preprocess.diagnostic_count == 0);
    c_test_preprocessed_token(arguments, &result, command_preprocess, 0, C_TOKEN_PREPROCESSING_NUMBER, S8("9"));

    CPreprocessResult function_macro = c_preprocess(arguments->arena,
                                                    S8("#define ADD(x, y) x + y\n"
                                                       "ADD(1, 2)\n"),
                                                    (CPreprocessOptions){0});
    BUSTER_TEST(arguments, function_macro.diagnostic_count == 0);
    BUSTER_TEST(arguments, function_macro.token_count == 4);
    c_test_preprocessed_token(arguments, &result, function_macro, 0, C_TOKEN_PREPROCESSING_NUMBER, S8("1"));
    c_test_preprocessed_token(arguments, &result, function_macro, 1, C_TOKEN_PUNCTUATOR, S8("+"));
    c_test_preprocessed_token(arguments, &result, function_macro, 2, C_TOKEN_PREPROCESSING_NUMBER, S8("2"));

    CPreprocessResult repeated_parameter_macro = c_preprocess(arguments->arena,
                                                              S8("#define LENGTH(T, count) "
                                                                 "((count) / (sizeof(T) * 8) + "
                                                                 "((count) % (sizeof(T) * 8) != 0))\n"
                                                                 "#define ARRAY(T, N, count) "
                                                                 "T N[LENGTH(T, count)]\n"
                                                                 "#define WORD_ARRAY(N, Count) "
                                                                 "ARRAY(unsigned long, N, "
                                                                 "(unsigned long)(Count))\n"
                                                                 "WORD_ARRAY(flags, 3);\n"),
                                                              (CPreprocessOptions){0});
    BUSTER_TEST(arguments, repeated_parameter_macro.diagnostic_count == 0);
    CParseResult repeated_parameter_parse = c_parse(arguments->arena, repeated_parameter_macro);
    BUSTER_TEST(arguments, repeated_parameter_parse.diagnostic_count == 0);

    CPreprocessResult multiline_macro = c_preprocess(arguments->arena,
                                                     S8("#define DECLARE(type, name) type name\n"
                                                        "DECLARE(\n"
                                                        "    unsigned long,\n"
                                                        "    multiline_value\n"
                                                        ");\n"),
                                                     (CPreprocessOptions){0});
    BUSTER_TEST(arguments, multiline_macro.diagnostic_count == 0);
    CParseResult multiline_macro_parse = c_parse(arguments->arena, multiline_macro);
    BUSTER_TEST(arguments, multiline_macro_parse.diagnostic_count == 0);
    BUSTER_TEST(arguments, multiline_macro_parse.declaration_count == 1);

    CPreprocessResult variadic_macro = c_preprocess(arguments->arena,
                                                    S8("#define TAIL(first, ...) __VA_ARGS__\n"
                                                       "TAIL(0, 1, 2)\n"),
                                                    (CPreprocessOptions){0});
    BUSTER_TEST(arguments, variadic_macro.diagnostic_count == 0);
    BUSTER_TEST(arguments, variadic_macro.token_count == 4);
    c_test_preprocessed_token(arguments, &result, variadic_macro, 0, C_TOKEN_PREPROCESSING_NUMBER, S8("1"));
    c_test_preprocessed_token(arguments, &result, variadic_macro, 1, C_TOKEN_PUNCTUATOR, S8(","));
    c_test_preprocessed_token(arguments, &result, variadic_macro, 2, C_TOKEN_PREPROCESSING_NUMBER, S8("2"));

    CPreprocessResult macro_operators = c_preprocess(arguments->arena,
                                                     S8("#define STRINGIFY(value) #value\n"
                                                        "#define PASTE(left, right) left ## right\n"
                                                        "#define OBJECT_PASTE object ## _name\n"
                                                        "#define GNU_TAIL(first, ...) "
                                                        "first, ## __VA_ARGS__\n"
                                                        "STRINGIFY(hello + \"world\")\n"
                                                        "PASTE(name, _suffix)\n"
                                                        "OBJECT_PASTE\n"
                                                        "GNU_TAIL(1)\n"),
                                                     (CPreprocessOptions){0});
    BUSTER_TEST(arguments, macro_operators.diagnostic_count == 0);
    BUSTER_TEST(arguments, macro_operators.token_count == 5);
    c_test_preprocessed_token(arguments, &result, macro_operators, 0, C_TOKEN_STRING_LITERAL, S8("\"hello + \\\"world\\\"\""));
    c_test_preprocessed_token(arguments, &result, macro_operators, 1, C_TOKEN_IDENTIFIER, S8("name_suffix"));
    c_test_preprocessed_token(arguments, &result, macro_operators, 2, C_TOKEN_IDENTIFIER, S8("object_name"));
    c_test_preprocessed_token(arguments, &result, macro_operators, 3, C_TOKEN_PREPROCESSING_NUMBER, S8("1"));

    CPreprocessResult macro_argument_prescan = c_preprocess(arguments->arena,
                                                            S8("#define AFTERX(value) X_ ## value\n"
                                                               "#define XAFTERX(value) AFTERX(value)\n"
                                                               "#define BUFSIZE TABLESIZE\n"
                                                               "#define TABLESIZE 1024\n"
                                                               "XAFTERX(BUFSIZE)\n"
                                                               "AFTERX(BUFSIZE)\n"),
                                                            (CPreprocessOptions){0});
    BUSTER_TEST(arguments, macro_argument_prescan.diagnostic_count == 0);
    BUSTER_TEST(arguments, macro_argument_prescan.token_count == 3);
    c_test_preprocessed_token(arguments, &result, macro_argument_prescan, 0, C_TOKEN_IDENTIFIER, S8("X_1024"));
    c_test_preprocessed_token(arguments, &result, macro_argument_prescan, 1, C_TOKEN_IDENTIFIER, S8("X_BUFSIZE"));

    CPreprocessResult conditional = c_preprocess(arguments->arena,
                                                 S8("#define ENABLED 1\n"
                                                    "#if defined(ENABLED) && "
                                                    "(ENABLED + 1 == 2)\n"
                                                    "kept\n"
                                                    "#else\n"
                                                    "dropped\n"
                                                    "#endif\n"
                                                    "#ifndef MISSING\n"
                                                    "also_kept\n"
                                                    "#endif\n"
                                                    "#if defined(MISSING) ? 0 : 1\n"
                                                    "conditional_kept\n"
                                                    "#endif\n"),
                                                 (CPreprocessOptions){0});
    BUSTER_TEST(arguments, conditional.diagnostic_count == 0);
    BUSTER_TEST(arguments, conditional.token_count == 4);
    c_test_preprocessed_token(arguments, &result, conditional, 0, C_TOKEN_IDENTIFIER, S8("kept"));
    c_test_preprocessed_token(arguments, &result, conditional, 1, C_TOKEN_IDENTIFIER, S8("also_kept"));
    c_test_preprocessed_token(arguments, &result, conditional, 2, C_TOKEN_IDENTIFIER, S8("conditional_kept"));

    CPreprocessResult directive_in_expression = c_preprocess(arguments->arena,
                                                             S8("int selected = (\n"
                                                                "#if 0\n"
                                                                "    1\n"
                                                                "#else\n"
                                                                "    2\n"
                                                                "#endif\n"
                                                                ");\n"),
                                                             (CPreprocessOptions){0});
    BUSTER_TEST(arguments, directive_in_expression.diagnostic_count == 0);
    CParseResult directive_in_expression_parse = c_parse(arguments->arena, directive_in_expression);
    BUSTER_TEST(arguments, directive_in_expression_parse.diagnostic_count == 0);
    BUSTER_TEST(arguments, directive_in_expression_parse.declaration_count == 1);

    CPreprocessResult macro_introduced_defined = c_preprocess(arguments->arena,
                                                              S8("#define IS_DRIVERKIT() defined(__DRIVERKIT_VERSION_MIN_REQUIRED)\n"
                                                                 "#if !IS_DRIVERKIT()\n"
                                                                 "int not_driverkit;\n"
                                                                 "#endif\n"),
                                                              (CPreprocessOptions){0});
    BUSTER_TEST(arguments, macro_introduced_defined.diagnostic_count == 0);
    CParseResult macro_introduced_defined_parse = c_parse(arguments->arena, macro_introduced_defined);
    BUSTER_TEST(arguments, macro_introduced_defined_parse.diagnostic_count == 0);
    BUSTER_TEST(arguments, macro_introduced_defined_parse.declaration_count == 1);

    CPreprocessResult inactive_objective_c = c_preprocess(arguments->arena,
                                                          S8("#if 0\n"
                                                             "@class Protocol;\n"
                                                             "#endif\n"
                                                             "int plain_c;\n"),
                                                          (CPreprocessOptions){0});
    BUSTER_TEST(arguments, inactive_objective_c.diagnostic_count == 0);
    CParseResult inactive_objective_c_parse = c_parse(arguments->arena, inactive_objective_c);
    BUSTER_TEST(arguments, inactive_objective_c_parse.diagnostic_count == 0);
    BUSTER_TEST(arguments, inactive_objective_c_parse.declaration_count == 1);

    CPreprocessResult pragma_pack = c_preprocess(arguments->arena,
                                                 S8("#pragma pack(push, 4)\n"
                                                    "typedef struct { char byte; long long value; } Packed;\n"
                                                    "#pragma pack(pop)\n"
                                                    "typedef struct { char byte; long long value; } Natural;\n"
                                                    "_Static_assert(sizeof(Packed) == 12, \"packed layout\");\n"
                                                    "_Static_assert(sizeof(Natural) == 16, \"natural layout\");\n"),
                                                 (CPreprocessOptions){0});
    BUSTER_TEST(arguments, pragma_pack.diagnostic_count == 0);
    bool packed_alignment_seen = false;
    bool natural_alignment_seen = false;
    for (u32 token_index = 0; token_index < pragma_pack.token_count; token_index += 1)
    {
        CToken token = pragma_pack.tokens[token_index];
        packed_alignment_seen |= string_equal(token.spelling, S8("Packed")) && token.pack_alignment == 4;
        natural_alignment_seen |= string_equal(token.spelling, S8("Natural")) && token.pack_alignment == 0;
    }
    BUSTER_TEST(arguments, packed_alignment_seen);
    BUSTER_TEST(arguments, natural_alignment_seen);
    CParseResult pragma_pack_parse = c_parse(arguments->arena, pragma_pack);
    BUSTER_TEST(arguments, pragma_pack_parse.diagnostic_count == 0);

    CPreprocessorDefinition diagnostic_text = {
        .name = S8("DIAGNOSTIC_TEXT"),
        .value = S8("expanded warning"),
    };
    CPreprocessResult preprocess_diagnostics = c_preprocess(arguments->arena,
                                                            S8("#warning direct warning\n"
                                                               "#warning DIAGNOSTIC_TEXT\n"
                                                               "#if 0\n"
                                                               "#error inactive error\n"
                                                               "#warning inactive warning\n"
                                                               "#endif\n"
                                                               "int diagnostic_value;\n"),
                                                            (CPreprocessOptions){
                                                                .definitions = &diagnostic_text,
                                                                .definition_count = 1,
                                                            });
    BUSTER_TEST(arguments, preprocess_diagnostics.diagnostic_count == 2);
    BUSTER_TEST(arguments, preprocess_diagnostics.error_count == 0);
    BUSTER_TEST(arguments, preprocess_diagnostics.warning_count == 2);
    if (preprocess_diagnostics.diagnostic_count == 2)
    {
        BUSTER_TEST(arguments, preprocess_diagnostics.diagnostics[0].kind == C_DIAGNOSTIC_PREPROCESSOR_WARNING);
        BUSTER_TEST(arguments, preprocess_diagnostics.diagnostics[0].severity == C_DIAGNOSTIC_WARNING);
        BUSTER_STRING_TEST(arguments, preprocess_diagnostics.diagnostics[0].message, S8("direct warning"));
        BUSTER_TEST(arguments, preprocess_diagnostics.diagnostics[1].kind == C_DIAGNOSTIC_PREPROCESSOR_WARNING);
        BUSTER_TEST(arguments, preprocess_diagnostics.diagnostics[1].severity == C_DIAGNOSTIC_WARNING);
        BUSTER_STRING_TEST(arguments, preprocess_diagnostics.diagnostics[1].message, S8("expanded warning"));
    }
    CPreprocessResult preprocess_error = c_preprocess(arguments->arena,
                                                      S8("#define ERROR_TEXT expanded error\n"
                                                         "#error ERROR_TEXT\n"
                                                         "#warning trailing warning\n"
                                                         "int after_error;\n"),
                                                      (CPreprocessOptions){0});
    BUSTER_TEST(arguments, preprocess_error.diagnostic_count == 2);
    BUSTER_TEST(arguments, preprocess_error.error_count == 1);
    BUSTER_TEST(arguments, preprocess_error.warning_count == 1);
    if (preprocess_error.diagnostic_count == 2)
    {
        BUSTER_TEST(arguments, preprocess_error.diagnostics[0].kind == C_DIAGNOSTIC_PREPROCESSOR_ERROR);
        BUSTER_TEST(arguments, preprocess_error.diagnostics[0].severity == C_DIAGNOSTIC_ERROR);
        BUSTER_STRING_TEST(arguments, preprocess_error.diagnostics[0].message, S8("expanded error"));
    }

    CPreprocessResult expanded_pragmas = c_preprocess(arguments->arena,
                                                      S8("#define PACK_DIRECTIVE \"pack(push, 4)\"\n"
                                                         "#define SAVED_VALUE 1\n"
                                                         "#define SAVED_FUNCTION(value) value + 1\n"
                                                         "_Pragma(PACK_DIRECTIVE)\n"
                                                         "_Pragma(\"push_macro(\\\"SAVED_VALUE\\\")\")\n"
                                                         "_Pragma(\"push_macro(\\\"SAVED_FUNCTION\\\")\")\n"
                                                         "#define SAVED_VALUE 2\n"
                                                         "#define SAVED_FUNCTION(value, extra) value + extra\n"
                                                         "typedef struct { char byte; long long value; } ExpandedPacked;\n"
                                                         "_Pragma(\"pack(push, 8)\") typedef struct { char byte; long long value; } ExpandedInlinePacked; _Pragma(\"pack(pop)\")\n"
                                                         "_Pragma(\"pop_macro(\\\"SAVED_FUNCTION\\\")\")\n"
                                                         "_Pragma(\"pop_macro(\\\"SAVED_VALUE\\\")\")\n"
                                                         "SAVED_VALUE SAVED_FUNCTION(2)\n"
                                                         "_Pragma(\"pack(pop)\")\n"
                                                         "typedef struct { char byte; long long value; } ExpandedNatural;\n"),
                                                      (CPreprocessOptions){0});
    BUSTER_TEST(arguments, expanded_pragmas.diagnostic_count == 0);
    BUSTER_TEST(arguments, expanded_pragmas.token_count != 0);
    bool expanded_packed_alignment_seen = false;
    bool expanded_inline_packed_alignment_seen = false;
    bool expanded_natural_alignment_seen = false;
    bool restored_value_seen = false;
    bool restored_function_seen = false;
    for (u32 token_index = 0; token_index < expanded_pragmas.token_count; token_index += 1)
    {
        CToken token = expanded_pragmas.tokens[token_index];
        expanded_packed_alignment_seen |= string_equal(token.spelling, S8("ExpandedPacked")) && token.pack_alignment == 4;
        expanded_inline_packed_alignment_seen |= string_equal(token.spelling, S8("ExpandedInlinePacked")) && token.pack_alignment == 8;
        expanded_natural_alignment_seen |= string_equal(token.spelling, S8("ExpandedNatural")) && token.pack_alignment == 0;
        restored_value_seen |= string_equal(token.spelling, S8("1"));
        restored_function_seen |= string_equal(token.spelling, S8("+"));
        BUSTER_TEST(arguments, token.kind != C_TOKEN_PRAGMA);
    }
    BUSTER_TEST(arguments, expanded_packed_alignment_seen);
    BUSTER_TEST(arguments, expanded_inline_packed_alignment_seen);
    BUSTER_TEST(arguments, expanded_natural_alignment_seen);
    BUSTER_TEST(arguments, restored_value_seen);
    BUSTER_TEST(arguments, restored_function_seen);

    CPreprocessResult windows_pragmas = c_preprocess(arguments->arena,
                                                     S8("__pragma(pack(push, 2))\n"
                                                        "#pragma GCC diagnostic push\n"
                                                        "#pragma clang diagnostic ignored \"-Wunknown\"\n"
                                                        "#pragma visibility push(default)\n"
                                                        "#pragma warning(disable: 4100)\n"
                                                        "#pragma comment(lib, \"ignored\")\n"
                                                        "#pragma region ignored\n"
                                                        "#pragma endregion\n"
                                                        "#pragma omp parallel\n"
                                                        "typedef struct { char byte; long long value; } WindowsPacked;\n"
                                                        "__pragma(pack(pop))\n"
                                                        "typedef struct { char byte; long long value; } WindowsNatural;\n"),
                                                     (CPreprocessOptions){
                                                         .target = {
                                                             .cpu_arch = CPU_ARCH_X86_64,
                                                             .os = OPERATING_SYSTEM_WINDOWS,
                                                         },
                                                     });
    BUSTER_TEST(arguments, windows_pragmas.diagnostic_count == 0);
    bool windows_packed_alignment_seen = false;
    bool windows_natural_alignment_seen = false;
    for (u32 token_index = 0; token_index < windows_pragmas.token_count; token_index += 1)
    {
        CToken token = windows_pragmas.tokens[token_index];
        windows_packed_alignment_seen |= string_equal(token.spelling, S8("WindowsPacked")) && token.pack_alignment == 2;
        windows_natural_alignment_seen |= string_equal(token.spelling, S8("WindowsNatural")) && token.pack_alignment == 0;
    }
    BUSTER_TEST(arguments, windows_packed_alignment_seen);
    BUSTER_TEST(arguments, windows_natural_alignment_seen);

    CPreprocessResult cross_target_clang_macros = c_preprocess(arguments->arena,
                                                                S8("#if defined(__clang__) && __clang_major__ == 18\n"
                                                                   "int clang_compatibility;\n"
                                                                   "#else\n"
                                                                   "#error missing clang compatibility macros\n"
                                                                   "#endif\n"),
                                                                (CPreprocessOptions){
                                                                    .target = {
                                                                        .cpu_arch = CPU_ARCH_X86_64,
                                                                        .os = OPERATING_SYSTEM_LINUX,
                                                                    },
                                                                });
    BUSTER_TEST(arguments, cross_target_clang_macros.diagnostic_count == 0);

    CPreprocessResult unmatched_conditional = c_preprocess(arguments->arena, S8("#if 1\nvalue\n"), (CPreprocessOptions){0});
    BUSTER_TEST(arguments, unmatched_conditional.diagnostic_count == 1);
    if (unmatched_conditional.diagnostic_count)
    {
        BUSTER_TEST(arguments, unmatched_conditional.diagnostics[0].kind == C_DIAGNOSTIC_UNMATCHED_CONDITIONAL);
    }

    CPreprocessResult builtins = c_preprocess(arguments->arena,
                                              S8("__STDC__ __STDC_VERSION__\n"
                                                 "__LINE__ __FILE__\n"
                                                 "__GNUC__ __GNUC_MINOR__ "
                                                 "__GNUC_PATCHLEVEL__\n"),
                                              (CPreprocessOptions){
                                                  .source_path = S8("builtins.c"),
                                              });
    BUSTER_TEST(arguments, builtins.diagnostic_count == 0);
    BUSTER_TEST(arguments, builtins.token_count == 8);
    c_test_preprocessed_token(arguments, &result, builtins, 0, C_TOKEN_PREPROCESSING_NUMBER, S8("1"));
    c_test_preprocessed_token(arguments, &result, builtins, 1, C_TOKEN_PREPROCESSING_NUMBER, S8("201710L"));
    c_test_preprocessed_token(arguments, &result, builtins, 2, C_TOKEN_PREPROCESSING_NUMBER, S8("2"));
    c_test_preprocessed_token(arguments, &result, builtins, 3, C_TOKEN_STRING_LITERAL, S8("\"builtins.c\""));
    c_test_preprocessed_token(arguments, &result, builtins, 4, C_TOKEN_PREPROCESSING_NUMBER, S8("4"));
    c_test_preprocessed_token(arguments, &result, builtins, 5, C_TOKEN_PREPROCESSING_NUMBER, S8("2"));
    c_test_preprocessed_token(arguments, &result, builtins, 6, C_TOKEN_PREPROCESSING_NUMBER, S8("1"));

    CPreprocessResult aarch64_android_builtins = c_preprocess(arguments->arena,
                                                              S8("#if defined(__ANDROID__) && "
                                                                 "defined(__linux__) && "
                                                                 "defined(__ELF__) && "
                                                                 "__ANDROID_API__ == 35 && "
                                                                 "__ANDROID_MIN_SDK_VERSION__ == 35 && "
                                                                 "__LP64__ && _LP64 && "
                                                                 "__SIZEOF_POINTER__ == 8 && "
                                                                 "__POINTER_WIDTH__ == 64 && "
                                                                 "__BYTE_ORDER__ == "
                                                                 "__ORDER_LITTLE_ENDIAN__\n"
                                                                 "typedef __CHAR8_TYPE__ TargetChar8;\n"
                                                                 "typedef __CHAR16_TYPE__ TargetChar16;\n"
                                                                 "typedef __CHAR32_TYPE__ TargetChar32;\n"
                                                                 "_Static_assert("
                                                                 "sizeof(TargetChar8) == 1,"
                                                                 " \"char8 width\");\n"
                                                                 "_Static_assert("
                                                                 "sizeof(TargetChar16) == 2,"
                                                                 " \"char16 width\");\n"
                                                                 "_Static_assert("
                                                                 "sizeof(TargetChar32) == 4,"
                                                                 " \"char32 width\");\n"
                                                                 "#else\n"
                                                                 "_Static_assert(0, \"target data model\");\n"
                                                                 "#endif\n"),
                                                              (CPreprocessOptions){
                                                                  .target =
                                                                      {
                                                                          .cpu_arch = CPU_ARCH_AARCH64,
                                                                          .os = OPERATING_SYSTEM_ANDROID,
                                                                          .os_version_major = 35,
                                                                      },
                                                              });
    BUSTER_TEST(arguments, aarch64_android_builtins.diagnostic_count == 0);
    CParseResult aarch64_android_builtins_parse = c_parse(arguments->arena, aarch64_android_builtins);
    BUSTER_TEST(arguments, aarch64_android_builtins_parse.diagnostic_count == 0);

    CPreprocessResult x64_windows_builtins = c_preprocess(arguments->arena,
                                                          S8("#if defined(_WIN64) && "
                                                             "!defined(__LP64__) && "
                                                             "!defined(_LP64) && "
                                                             "__LONG_WIDTH__ == 32 && "
                                                             "__SIZEOF_POINTER__ == 8\n"
                                                             "int windows_data_model;\n"
                                                             "#else\n"
                                                             "_Static_assert(0, \"target data model\");\n"
                                                             "#endif\n"),
                                                          (CPreprocessOptions){
                                                              .target =
                                                                  {
                                                                      .cpu_arch = CPU_ARCH_X86_64,
                                                                      .os = OPERATING_SYSTEM_WINDOWS,
                                                                  },
                                                          });
    BUSTER_TEST(arguments, x64_windows_builtins.diagnostic_count == 0);
    CParseResult x64_windows_builtins_parse = c_parse(arguments->arena, x64_windows_builtins);
    BUSTER_TEST(arguments, x64_windows_builtins_parse.diagnostic_count == 0);

    CPreprocessResult aarch64_macos_builtins = c_preprocess(arguments->arena,
                                                            S8("#if defined(__APPLE__) && "
                                                               "defined(__MACH__) && "
                                                               "defined(__BUSTER__) && "
                                                               "defined(__BUSTER_TARGET_MACOS__) && "
                                                               "__APPLE_CC__ >= 6000 && "
                                                               "defined(__arm64__) && "
                                                               "__has_builtin(__is_target_arch) && "
                                                               "__is_target_arch(arm64) && "
                                                               "__is_target_vendor(apple) && "
                                                               "__is_target_os(macos) && "
                                                               "!__is_target_os(ios) && "
                                                               "!__is_target_environment(simulator)\n"
                                                               "int macos_target;\n"
                                                               "#else\n"
                                                               "_Static_assert(0, \"macOS target macros\");\n"
                                                               "#endif\n"),
                                                            (CPreprocessOptions){
                                                                .target =
                                                                    {
                                                                        .cpu_arch = CPU_ARCH_AARCH64,
                                                                        .os = OPERATING_SYSTEM_MACOS,
                                                                    },
                                                            });
    BUSTER_TEST(arguments, aarch64_macos_builtins.diagnostic_count == 0);
    CParseResult aarch64_macos_builtins_parse = c_parse(arguments->arena, aarch64_macos_builtins);
    BUSTER_TEST(arguments, aarch64_macos_builtins_parse.diagnostic_count == 0);

    CPreprocessResult include = c_preprocess(arguments->arena,
                                             S8("#include \"basic_c_include.h\"\n"
                                                "INCLUDED_VALUE\n"),
                                             (CPreprocessOptions){
                                                 .source_path = S8("tests/include_test.c"),
                                             });
    BUSTER_TEST(arguments, include.diagnostic_count == 0);
    BUSTER_TEST(arguments, include.token_count == 2);
    c_test_preprocessed_token(arguments, &result, include, 0, C_TOKEN_PREPROCESSING_NUMBER, S8("37"));
    CPreprocessResult import = c_preprocess(arguments->arena,
                                            S8("#import \"basic_c_include.h\"\n"
                                               "#import \"basic_c_include.h\"\n"
                                               "INCLUDED_VALUE\n"),
                                            (CPreprocessOptions){
                                                .source_path = S8("tests/import_test.c"),
                                            });
    BUSTER_TEST(arguments, import.diagnostic_count == 0);
    BUSTER_TEST(arguments, import.token_count == 2);
    c_test_preprocessed_token(arguments, &result, import, 0, C_TOKEN_PREPROCESSING_NUMBER, S8("37"));
    CPreprocessResult builtin_headers = c_preprocess(arguments->arena,
                                                     S8("#include <stdbool.h>\n"
                                                        "#include <stdalign.h>\n"
                                                        "#include <stdarg.h>\n"
                                                        "#include <stddef.h>\n"
                                                        "#include <limits.h>\n"
                                                        "alignas(8) int aligned_value;\n"
                                                        "size_t builtin_size;\n"
                                                        "ptrdiff_t builtin_difference;\n"
                                                        "max_align_t builtin_alignment;\n"
                                                        "_Static_assert(CHAR_BIT == 8,"
                                                        " \"character width\");\n"
                                                        "_Static_assert(INT_MAX == 2147483647,"
                                                        " \"integer maximum\");\n"
                                                        "int variadic_value(bool enabled, ...) {\n"
                                                        "    va_list arguments;\n"
                                                        "    va_start(arguments, enabled);\n"
                                                        "    int value = va_arg(arguments, int);\n"
                                                        "    va_end(arguments);\n"
                                                        "    return enabled ? value : false;\n"
                                                        "}\n"),
                                                     (CPreprocessOptions){
                                                         .source_path = S8("builtin-headers.c"),
                                                     });
    BUSTER_TEST(arguments, builtin_headers.diagnostic_count == 0);
    CParseResult builtin_headers_parse = c_parse(arguments->arena, builtin_headers);
    BUSTER_TEST(arguments, builtin_headers_parse.diagnostic_count == 0);

    FileMapRead hermetic_c_source_map = file_map_read(arguments->arena, S8("tests/fuzz/valid_c.c"), (FileReadOptions){0});
    String8 hermetic_c_source = BYTE_SLICE_TO_STRING(8, hermetic_c_source_map.bytes);
    BUSTER_TEST(arguments, hermetic_c_source.pointer != 0);
    CPreprocessResult hermetic_builtin_headers = c_preprocess(arguments->arena, hermetic_c_source,
                                                              (CPreprocessOptions){
                                                                  .source_path = S8("fuzz.c"),
                                                                  .target = target_native,
                                                                  .data_layout = target_data_layout(target_native),
                                                                  .disable_external_includes = true,
                                                              });
    BUSTER_TEST(arguments, hermetic_builtin_headers.diagnostic_count == 0);
    CParserResult hermetic_syntax = c_parse_ast(arguments->arena, hermetic_builtin_headers);
    BUSTER_TEST(arguments, hermetic_syntax.diagnostic_count == 0);
    CIRLowerResult hermetic_ir = c_analyze(arguments->arena, S8("fuzz.c"), hermetic_builtin_headers, hermetic_syntax, target_native);
    BUSTER_TEST(arguments, hermetic_ir.diagnostic_count == 0);
    BUSTER_TEST(arguments, hermetic_ir.program != 0);
    if (hermetic_ir.program)
    {
        BUSTER_TEST(arguments, ir_validate_canonical_module(hermetic_ir.program, &hermetic_ir.program->modules[0]).error == IR_VALIDATION_NONE);
    }
    file_map_unmap(hermetic_c_source_map);
    CPreprocessResult hermetic_blocked_headers = c_preprocess(arguments->arena,
                                                              S8("#include <stddef.h>\n"
                                                                 "#include \"basic_c_include.h\"\n"
                                                                 "#include <basic_c_include.h>\n"
                                                                 "#include \"/definitely/missing/buster-header.h\"\n"
                                                                 "size_t value;\n"),
                                                              (CPreprocessOptions){
                                                                  .source_path = S8("fuzz.c"),
                                                                  .target = target_native,
                                                                  .data_layout = target_data_layout(target_native),
                                                                  .disable_external_includes = true,
                                                              });
    BUSTER_TEST(arguments, hermetic_blocked_headers.diagnostic_count == 3);
    for (u64 diagnostic_index = 0; diagnostic_index < hermetic_blocked_headers.diagnostic_count; diagnostic_index += 1)
    {
        BUSTER_TEST(arguments, hermetic_blocked_headers.diagnostics[diagnostic_index].kind == C_DIAGNOSTIC_INCLUDE_NOT_FOUND);
    }
    String8 feature_include_paths[] = {
        S8("tests"),
        S8("tests/include_first"),
        S8("tests/include_second"),
    };
    CPreprocessResult feature_queries = c_preprocess(arguments->arena,
                                                     S8("#define HEADER_AVAILABLE(header)"
                                                        " __has_include(header)\n"
                                                        "#if HEADER_AVAILABLE("
                                                        "\"basic_c_include.h\")\n"
                                                        "11\n"
                                                        "#else\n"
                                                        "12\n"
                                                        "#endif\n"
                                                        "#if __has_include("
                                                        "<basic_c_include.h>)\n"
                                                        "21\n"
                                                        "#else\n"
                                                        "22\n"
                                                        "#endif\n"
                                                        "#if __has_include("
                                                        "\"missing_buster_header.h\")\n"
                                                        "31\n"
                                                        "#else\n"
                                                        "32\n"
                                                        "#endif\n"
                                                        "#if __has_builtin("
                                                        "__builtin_debugtrap) &&"
                                                        " !__has_builtin("
                                                        "__builtin_not_implemented)\n"
                                                        "51\n"
                                                        "#else\n"
                                                        "52\n"
                                                        "#endif\n"
                                                        "#if __has_attribute(vector_size)"
                                                        " && !__has_feature("
                                                        "not_implemented)\n"
                                                        "61\n"
                                                        "#else\n"
                                                        "62\n"
                                                        "#endif\n"
                                                        "#include <feature_next.h>\n"
                                                        "#include <actual_next.h>\n"),
                                                     (CPreprocessOptions){
                                                         .include_paths = feature_include_paths,
                                                         .source_path = S8("tests/feature_queries.c"),
                                                         .include_path_count = BUSTER_ARRAY_LENGTH(feature_include_paths),
                                                     });
    BUSTER_TEST(arguments, feature_queries.diagnostic_count == 0);
    BUSTER_TEST(arguments, feature_queries.token_count == 8);
    String8 feature_query_values[] = {
        S8("11"), S8("21"), S8("32"), S8("51"), S8("61"), S8("41"), S8("73"),
    };
    for (u32 query_index = 0; query_index < BUSTER_ARRAY_LENGTH(feature_query_values); query_index += 1)
    {
        c_test_preprocessed_token(arguments, &result, feature_queries, query_index, C_TOKEN_PREPROCESSING_NUMBER, feature_query_values[query_index]);
    }
    CPreprocessResult line_remapping = c_preprocess(arguments->arena,
                                                    S8("#define REMAPPED_LINE 200\n"
                                                       "#line REMAPPED_LINE \"generated.c\"\n"
                                                       "__LINE__ __FILE__ token\n"
                                                       "#line 7\n"
                                                       "__LINE__ __FILE__ token\n"),
                                                    (CPreprocessOptions){
                                                        .source_path = S8("tests/original.c"),
                                                    });
    BUSTER_TEST(arguments, line_remapping.diagnostic_count == 0);
    BUSTER_TEST(arguments, line_remapping.token_count == 7);
    String8 line_remapping_values[] = {
        S8("200"), S8("\"generated.c\""), S8("token"), S8("7"), S8("\"generated.c\""), S8("token"),
    };
    CTokenKind line_remapping_kinds[] = {
        C_TOKEN_PREPROCESSING_NUMBER, C_TOKEN_STRING_LITERAL, C_TOKEN_IDENTIFIER, C_TOKEN_PREPROCESSING_NUMBER, C_TOKEN_STRING_LITERAL, C_TOKEN_IDENTIFIER,
    };
    for (u32 remapping_index = 0; remapping_index < BUSTER_ARRAY_LENGTH(line_remapping_values); remapping_index += 1)
    {
        c_test_preprocessed_token(arguments, &result, line_remapping, remapping_index, line_remapping_kinds[remapping_index],
                                  line_remapping_values[remapping_index]);
    }
    if (line_remapping.token_count >= 6)
    {
        BUSTER_TEST(arguments, line_remapping.tokens[0].location.line == 200);
        BUSTER_TEST(arguments, line_remapping.tokens[2].location.line == 200);
        BUSTER_TEST(arguments, line_remapping.tokens[3].location.line == 7);
        BUSTER_TEST(arguments, line_remapping.tokens[5].location.line == 7);
    }
    CPreprocessResult gnu_line_markers = c_preprocess(arguments->arena,
                                                      S8("# 42 \"generated.i\" 1 3 4\n"
                                                         "__LINE__ __FILE__ token\n"
                                                         "# 9 \"original.c\" 2\n"
                                                         "__LINE__ __FILE__ token\n"),
                                                      (CPreprocessOptions){
                                                          .source_path = S8("tests/preprocessed.i"),
                                                      });
    BUSTER_TEST(arguments, gnu_line_markers.diagnostic_count == 0);
    BUSTER_TEST(arguments, gnu_line_markers.token_count == 7);
    String8 gnu_line_marker_values[] = {
        S8("42"), S8("\"generated.i\""), S8("token"), S8("9"), S8("\"original.c\""), S8("token"),
    };
    for (u32 marker_index = 0; marker_index < BUSTER_ARRAY_LENGTH(gnu_line_marker_values); marker_index += 1)
    {
        c_test_preprocessed_token(arguments, &result, gnu_line_markers, marker_index,
                                  marker_index == 1 || marker_index == 4   ? C_TOKEN_STRING_LITERAL
                                  : marker_index == 2 || marker_index == 5 ? C_TOKEN_IDENTIFIER
                                                                           : C_TOKEN_PREPROCESSING_NUMBER,
                                  gnu_line_marker_values[marker_index]);
    }
    if (gnu_line_markers.token_count >= 6)
    {
        BUSTER_TEST(arguments, gnu_line_markers.tokens[0].location.line == 42);
        BUSTER_TEST(arguments, gnu_line_markers.tokens[3].location.line == 9);
    }
    CPreprocessResult invalid_line = c_preprocess(arguments->arena,
                                                  S8("#line 0\n"
                                                     "#line 2 \"generated.c\" extra\n"
                                                     "# 3 \"generated.i\" 5\n"),
                                                  (CPreprocessOptions){
                                                      .source_path = S8("tests/invalid_line.c"),
                                                  });
    BUSTER_TEST(arguments, invalid_line.diagnostic_count == 3);
    for (u32 diagnostic_index = 0; diagnostic_index < invalid_line.diagnostic_count; diagnostic_index += 1)
    {
        BUSTER_TEST(arguments, invalid_line.diagnostics[diagnostic_index].kind == C_DIAGNOSTIC_INVALID_LINE);
    }
    CPreprocessResult local_declaration_tokens = c_preprocess(arguments->arena,
                                                              S8("typedef struct SignalInfo {\n"
                                                                 "    int code;\n"
                                                                 "    union {\n"
                                                                 "        int value;\n"
                                                                 "        struct {\n"
                                                                 "            void *lower;\n"
                                                                 "            void *upper;\n"
                                                                 "        } bounds;\n"
                                                                 "    } fields;\n"
                                                                 "} SignalInfo;\n"
                                                                 "typedef unsigned char Byte;\n"
                                                                 "typedef unsigned short Wide, *WidePointer, **WidePointerPointer;\n"
                                                                 "static Byte const byte_table[2];\n"
                                                                 "typedef int Callback("
                                                                 "int, void *user_data);\n"
                                                                 "typedef int (*CallbackPointer)(int);\n"
                                                                 "int signal_offset(void)\n"
                                                                 "{\n"
                                                                 "    return __builtin_offsetof("
                                                                 "SignalInfo, code);\n"
                                                                 "}\n"
                                                                 "int recursive_input(int value)\n"
                                                                 "{\n"
                                                                 "    if (value)\n"
                                                                 "        return recursive_input(value - 1);\n"
                                                                 "    return value;\n"
                                                                 "}\n"
                                                                 "int local_declarations(void)\n"
                                                                 "{\n"
                                                                 "    typedef struct LocalPair {\n"
                                                                 "        int left;\n"
                                                                 "        int right;\n"
                                                                 "    } LocalPair;\n"
                                                                 "    typedef enum Local {\n"
                                                                 "        LOCAL_FIRST = 1u << 2,\n"
                                                                 "        LOCAL_SECOND,\n"
                                                                 "        LOCAL_THIRD ="
                                                                 " (unsigned int)LOCAL_SECOND << 1\n"
                                                                 "    } Local;\n"
                                                                 "    _Static_assert("
                                                                 "LOCAL_THIRD == 10,"
                                                                 " \"local enum value\");\n"
                                                                 "    int first = 1,"
                                                                 " second = first + 1,"
                                                                 " *pointer = &second;\n"
                                                                 "    __attribute__((unused))"
                                                                 " Local value = LOCAL_SECOND;\n"
                                                                 "    int offset = __builtin_offsetof("
                                                                 "SignalInfo, code);\n"
                                                                 "    Callback *callback;\n"
                                                                 "    LocalPair pair;\n"
                                                                 "    return value + *pointer;\n"
                                                                 "}\n"),
                                                              (CPreprocessOptions){0});
    CParseResult local_declarations = c_parse(arguments->arena, local_declaration_tokens);
    BUSTER_TEST(arguments, local_declarations.diagnostic_count == 0);
    CPreprocessResult interposed_attribute_tokens = c_preprocess(arguments->arena,
                                                                 S8("static __attribute__((always_inline))"
                                                                    " inline int attributed_add("
                                                                    "int x, int y)"
                                                                    "{ return x + y; }\n"),
                                                                 (CPreprocessOptions){0});
    CParseResult interposed_attribute_parse = c_parse(arguments->arena, interposed_attribute_tokens);
    BUSTER_TEST(arguments, interposed_attribute_parse.diagnostic_count == 0);
    BUSTER_TEST(arguments, interposed_attribute_parse.declaration_count == 1);
    if (interposed_attribute_parse.declaration_count == 1)
    {
        CDeclaration attributed = interposed_attribute_parse.declarations[0];
        BUSTER_TEST(arguments, attributed.kind == C_DECLARATION_FUNCTION);
        BUSTER_TEST(arguments, attributed.parameter_count == 2);
        BUSTER_TEST(arguments, attributed.type.value != C_ID_UNDERLYING_INVALID);
    }
    for (u32 use_index = 0; use_index < interposed_attribute_parse.identifier_use_count; use_index += 1)
    {
        CIdentifierUse use = interposed_attribute_parse.identifier_uses[use_index];
        BUSTER_TEST(arguments, use.entity.value != C_ID_UNDERLYING_INVALID);
    }
    bool found_callback_typedef = false;
    bool found_callback_pointer_typedef = false;
    bool found_signal_info_typedef = false;
    bool found_const_typedef_object = false;
    bool found_local_typedef = false;
    bool found_local_pair_typedef = false;
    bool found_local_first = false;
    bool found_local_second = false;
    bool found_local_third = false;
    bool found_wide = false;
    bool found_wide_pointer = false;
    bool found_wide_pointer_pointer = false;
    bool found_first = false;
    bool found_second = false;
    bool found_pointer = false;
    bool found_callback = false;
    for (u32 entity_index = 0; entity_index < local_declarations.entity_count; entity_index += 1)
    {
        CEntity* entity = &local_declarations.entities[entity_index];
        found_callback_typedef |= entity->kind == C_ENTITY_TYPEDEF && string_equal(entity->name, S8("Callback"));
        found_callback_pointer_typedef |=
            entity->kind == C_ENTITY_TYPEDEF && entity->type.value != C_ID_UNDERLYING_INVALID && string_equal(entity->name, S8("CallbackPointer"));
        if (entity->kind == C_ENTITY_TYPEDEF && entity->type.value < local_declarations.type_count)
        {
            CType* type = &local_declarations.types[entity->type.value];
            found_wide |= string_equal(entity->name, S8("Wide")) && type->kind == C_TYPE_UNSIGNED_SHORT;
            if (string_equal(entity->name, S8("WidePointer")) && type->kind == C_TYPE_POINTER &&
                type->element_type.value < local_declarations.type_count)
            {
                found_wide_pointer = local_declarations.types[type->element_type.value].kind == C_TYPE_UNSIGNED_SHORT;
            }
            if (string_equal(entity->name, S8("WidePointerPointer")) && type->kind == C_TYPE_POINTER &&
                type->element_type.value < local_declarations.type_count)
            {
                CType* pointer = &local_declarations.types[type->element_type.value];
                found_wide_pointer_pointer = pointer->kind == C_TYPE_POINTER && pointer->element_type.value < local_declarations.type_count &&
                                             local_declarations.types[pointer->element_type.value].kind == C_TYPE_UNSIGNED_SHORT;
            }
        }
        found_signal_info_typedef |=
            entity->kind == C_ENTITY_TYPEDEF && entity->type.value != C_ID_UNDERLYING_INVALID && string_equal(entity->name, S8("SignalInfo"));
        if (string_equal(entity->name, S8("byte_table")) && entity->type.value < local_declarations.type_count)
        {
            CType* array_type = &local_declarations.types[entity->type.value];
            if (array_type->kind == C_TYPE_ARRAY && array_type->element_type.value < local_declarations.type_count)
            {
                found_const_typedef_object = local_declarations.types[array_type->element_type.value].is_const;
            }
        }
        found_local_typedef |= entity->kind == C_ENTITY_TYPEDEF && string_equal(entity->name, S8("Local"));
        found_local_pair_typedef |= entity->kind == C_ENTITY_TYPEDEF && string_equal(entity->name, S8("LocalPair"));
        found_local_first |= entity->kind == C_ENTITY_ENUMERATOR && string_equal(entity->name, S8("LOCAL_FIRST")) && entity->constant_value == 4;
        found_local_second |= entity->kind == C_ENTITY_ENUMERATOR && string_equal(entity->name, S8("LOCAL_SECOND")) && entity->constant_value == 5;
        found_local_third |= entity->kind == C_ENTITY_ENUMERATOR && string_equal(entity->name, S8("LOCAL_THIRD")) && entity->constant_value == 10;
        found_first |= string_equal(entity->name, S8("first"));
        found_second |= string_equal(entity->name, S8("second"));
        found_pointer |= string_equal(entity->name, S8("pointer"));
        found_callback |= string_equal(entity->name, S8("callback"));
    }
    BUSTER_TEST(arguments, found_callback_typedef);
    BUSTER_TEST(arguments, found_callback_pointer_typedef);
    BUSTER_TEST(arguments, found_signal_info_typedef);
    BUSTER_TEST(arguments, found_const_typedef_object);
    BUSTER_TEST(arguments, found_local_typedef);
    BUSTER_TEST(arguments, found_local_pair_typedef);
    BUSTER_TEST(arguments, found_local_first);
    BUSTER_TEST(arguments, found_local_second);
    BUSTER_TEST(arguments, found_local_third);
    BUSTER_TEST(arguments, found_wide);
    BUSTER_TEST(arguments, found_wide_pointer);
    BUSTER_TEST(arguments, found_wide_pointer_pointer);
    BUSTER_TEST(arguments, found_first);
    BUSTER_TEST(arguments, found_second);
    BUSTER_TEST(arguments, found_pointer);
    BUSTER_TEST(arguments, found_callback);

    CPreprocessResult static_assert_tokens = c_preprocess(arguments->arena,
                                                          S8("enum StaticValue {\n"
                                                             "    STATIC_VALUE = 3\n"
                                                             "};\n"
                                                             "typedef struct StaticPair {\n"
                                                             "    int values[(2)];\n"
                                                             "} StaticPair;\n"
                                                             "typedef union StaticLayout {\n"
                                                             "    struct { int first; int second; };\n"
                                                             "    int values[4];\n"
                                                             "} StaticLayout;\n"
                                                             "_Static_assert("
                                                             "STATIC_VALUE == 3,"
                                                             " \"enum value\");\n"
                                                             "_Static_assert("
                                                             "sizeof(StaticPair) == 8,"
                                                             " \"pair size\");\n"
                                                             "_Static_assert("
                                                             "sizeof(StaticLayout) == 16,"
                                                             " \"anonymous layout size\");\n"
                                                             "int member_static_assert(void) {\n"
                                                             "    StaticPair pair = {0};\n"
                                                             "    _Static_assert("
                                                             "sizeof(pair.values[0]) == 4,"
                                                             " \"member element size\");\n"
                                                             "    return 0;\n"
                                                             "}\n"),
                                                          (CPreprocessOptions){0});
    CParserResult static_assert_syntax = c_parse_ast(arguments->arena, static_assert_tokens);
    u32 parsed_global_static_assert_count = 0;
    u32 parsed_local_static_assert_count = 0;
    for (CParserDeclaration* syntax_declaration = static_assert_syntax.first_declaration; syntax_declaration; syntax_declaration = syntax_declaration->next)
    {
        parsed_global_static_assert_count += syntax_declaration->kind == C_PARSER_DECLARATION_STATIC_ASSERT;
        if (syntax_declaration->kind == C_PARSER_DECLARATION_FUNCTION)
        {
            for (CParserStatement* statement = syntax_declaration->first_statement; statement; statement = statement->next)
            {
                parsed_local_static_assert_count += statement->kind == C_PARSER_STATEMENT_STATIC_ASSERT;
            }
        }
    }
    BUSTER_TEST(arguments, static_assert_syntax.diagnostic_count == 0);
    BUSTER_TEST(arguments, parsed_global_static_assert_count == 3);
    BUSTER_TEST(arguments, parsed_local_static_assert_count == 1);
    CParseResult static_assert_parse = c_parse(arguments->arena, static_assert_tokens);
    BUSTER_TEST(arguments, static_assert_parse.diagnostic_count == 0);

    CPreprocessResult failed_static_assert_tokens = c_preprocess(arguments->arena, S8("_Static_assert(0, \"expected failure\");\n"), (CPreprocessOptions){0});
    CParserResult failed_static_assert_syntax = c_parse_ast(arguments->arena, failed_static_assert_tokens);
    BUSTER_TEST(arguments, failed_static_assert_syntax.diagnostic_count == 0);
    CIRLowerResult failed_static_assert_analysis =
        c_analyze(arguments->arena, S8("failed-static-assert.c"), failed_static_assert_tokens, failed_static_assert_syntax, target_native);
    BUSTER_TEST(arguments, failed_static_assert_analysis.diagnostic_count == 1);
    CParseResult failed_static_assert_parse = c_parse(arguments->arena, failed_static_assert_tokens);
    BUSTER_TEST(arguments, failed_static_assert_parse.diagnostic_count == 1);
    if (failed_static_assert_parse.diagnostic_count)
    {
        BUSTER_TEST(arguments, failed_static_assert_parse.diagnostics[0].kind == C_DIAGNOSTIC_STATIC_ASSERT_FAILED);
        BUSTER_STRING_TEST(arguments, failed_static_assert_parse.diagnostics[0].message,
                           S8("static assertion failed: "
                              "\"expected failure\""));
    }

    CPreprocessResult declaration_tokens = c_preprocess(arguments->arena,
                                                        S8("typedef unsigned long Size;\n"
                                                           "extern int value;\n"
                                                           "static int add(int left, int right);\n"
                                                           "int main(int count, char **values)\n"
                                                           "{\n"
                                                           "    if (count) { return add(count, 1); }\n"
                                                           "    return values != 0;\n"
                                                           "}\n"),
                                                        (CPreprocessOptions){0});
    CParseResult declarations = c_parse(arguments->arena, declaration_tokens);
    BUSTER_TEST(arguments, declarations.diagnostic_count == 0);
    BUSTER_TEST(arguments, declarations.declaration_count == 4);
    BUSTER_TEST(arguments, declarations.entity_count == 8);
    BUSTER_TEST(arguments, declarations.scope_count == 4);
    BUSTER_TEST(arguments, declarations.scope_count == 4 && declarations.scopes[0].entity_count == 4);
    if (declarations.declaration_count == 4)
    {
        BUSTER_TEST(arguments, declarations.declarations[0].kind == C_DECLARATION_TYPEDEF);
        BUSTER_TEST(arguments, declarations.declarations[0].entity.value == 0);
        BUSTER_TEST(arguments, declarations.entities[0].kind == C_ENTITY_TYPEDEF);
        BUSTER_TEST(arguments, declarations.entities[0].declaration_index == 0);
        BUSTER_TEST(arguments, declarations.entities[0].scope.value == 0);
        BUSTER_STRING_TEST(arguments, declarations.declarations[0].name, S8("Size"));
        CType* size_type = c_type_from_id(&declarations, declarations.declarations[0].type);
        BUSTER_TEST(arguments, size_type && size_type->kind == C_TYPE_UNSIGNED_LONG);
        BUSTER_TEST(arguments, declarations.declarations[1].kind == C_DECLARATION_OBJECT);
        BUSTER_STRING_TEST(arguments, declarations.declarations[1].name, S8("value"));
        CType* value_type = c_type_from_id(&declarations, declarations.declarations[1].type);
        BUSTER_TEST(arguments, value_type && value_type->kind == C_TYPE_INT);
        BUSTER_TEST(arguments, declarations.declarations[2].kind == C_DECLARATION_FUNCTION);
        BUSTER_TEST(arguments, !declarations.declarations[2].is_definition);
        CType* add_type = c_type_from_id(&declarations, declarations.declarations[2].type);
        BUSTER_TEST(arguments, add_type && add_type->kind == C_TYPE_FUNCTION);
        BUSTER_TEST(arguments, add_type && add_type->parameter_count == 2);
        BUSTER_TEST(arguments, declarations.declarations[2].parameter_count == 2);
        BUSTER_TEST(arguments, declarations.declarations[2].scope.value != C_ID_UNDERLYING_INVALID);
        if (add_type && add_type->parameter_count == 2)
        {
            CParameter left = declarations.parameters[add_type->parameter_start];
            CParameter right = declarations.parameters[add_type->parameter_start + 1];
            BUSTER_STRING_TEST(arguments, left.name, S8("left"));
            BUSTER_STRING_TEST(arguments, right.name, S8("right"));
            CType* left_type = c_type_from_id(&declarations, left.type);
            CType* right_type = c_type_from_id(&declarations, right.type);
            BUSTER_TEST(arguments, left_type && left_type->kind == C_TYPE_INT);
            BUSTER_TEST(arguments, right_type && right_type->kind == C_TYPE_INT);
            BUSTER_TEST(arguments, left.entity.value != C_ID_UNDERLYING_INVALID);
            BUSTER_TEST(arguments, right.entity.value != C_ID_UNDERLYING_INVALID);
            BUSTER_TEST(arguments, declarations.entities[left.entity.value].scope.value == declarations.declarations[2].scope.value);
        }
        BUSTER_STRING_TEST(arguments, declarations.declarations[3].name, S8("main"));
        BUSTER_TEST(arguments, declarations.declarations[3].is_definition);
        BUSTER_TEST(arguments, declarations.declarations[3].body_token_count != 0);
        CType* main_type = c_type_from_id(&declarations, declarations.declarations[3].type);
        BUSTER_TEST(arguments, main_type && main_type->kind == C_TYPE_FUNCTION);
        BUSTER_TEST(arguments, main_type && main_type->parameter_count == 2);
        if (main_type && main_type->parameter_count == 2)
        {
            CParameter values = declarations.parameters[main_type->parameter_start + 1];
            CType* outer_pointer = c_type_from_id(&declarations, values.type);
            CType* inner_pointer = outer_pointer ? c_type_from_id(&declarations, outer_pointer->element_type) : 0;
            CType* character = inner_pointer ? c_type_from_id(&declarations, inner_pointer->element_type) : 0;
            BUSTER_STRING_TEST(arguments, values.name, S8("values"));
            BUSTER_TEST(arguments, outer_pointer && outer_pointer->kind == C_TYPE_POINTER);
            BUSTER_TEST(arguments, inner_pointer && inner_pointer->kind == C_TYPE_POINTER);
            BUSTER_TEST(arguments, character && character->kind == C_TYPE_CHAR);
        }
    }

    CPreprocessResult array_declaration_tokens = c_preprocess(arguments->arena,
                                                              S8("int matrix[2][3];\n"
                                                                 "int sum(int values[static 4], "
                                                                 "int table[][3]);\n"),
                                                              (CPreprocessOptions){0});
    CParseResult array_declarations = c_parse(arguments->arena, array_declaration_tokens);
    BUSTER_TEST(arguments, array_declarations.diagnostic_count == 0);
    BUSTER_TEST(arguments, array_declarations.declaration_count == 2);
    if (array_declarations.declaration_count == 2)
    {
        CType* matrix_outer = c_type_from_id(&array_declarations, array_declarations.declarations[0].type);
        CType* matrix_inner = matrix_outer ? c_type_from_id(&array_declarations, matrix_outer->element_type) : 0;
        CType* matrix_element = matrix_inner ? c_type_from_id(&array_declarations, matrix_inner->element_type) : 0;
        BUSTER_TEST(arguments, matrix_outer && matrix_outer->kind == C_TYPE_ARRAY);
        BUSTER_TEST(arguments, matrix_inner && matrix_inner->kind == C_TYPE_ARRAY);
        BUSTER_TEST(arguments, matrix_element && matrix_element->kind == C_TYPE_INT);
        if (matrix_outer && matrix_inner)
        {
            CArrayBound outer_bound = array_declarations.array_bounds[matrix_outer->array_bound];
            CArrayBound inner_bound = array_declarations.array_bounds[matrix_inner->array_bound];
            BUSTER_TEST(arguments, outer_bound.token_count == 1);
            BUSTER_TEST(arguments, inner_bound.token_count == 1);
            BUSTER_STRING_TEST(arguments, array_declaration_tokens.tokens[outer_bound.token_start].spelling, S8("2"));
            BUSTER_STRING_TEST(arguments, array_declaration_tokens.tokens[inner_bound.token_start].spelling, S8("3"));
        }
        CType* sum_type = c_type_from_id(&array_declarations, array_declarations.declarations[1].type);
        BUSTER_TEST(arguments, sum_type && sum_type->kind == C_TYPE_FUNCTION);
        BUSTER_TEST(arguments, sum_type && sum_type->parameter_count == 2);
        if (sum_type && sum_type->parameter_count == 2)
        {
            CType* values_type = c_type_from_id(&array_declarations, array_declarations.parameters[sum_type->parameter_start].type);
            BUSTER_TEST(arguments, values_type && values_type->kind == C_TYPE_ARRAY);
            BUSTER_TEST(arguments, values_type && array_declarations.array_bounds[values_type->array_bound].is_static);
            CType* table_outer = c_type_from_id(&array_declarations, array_declarations.parameters[sum_type->parameter_start + 1].type);
            CType* table_inner = table_outer ? c_type_from_id(&array_declarations, table_outer->element_type) : 0;
            BUSTER_TEST(arguments, table_outer && table_outer->kind == C_TYPE_ARRAY);
            BUSTER_TEST(arguments, table_inner && table_inner->kind == C_TYPE_ARRAY);
            BUSTER_TEST(arguments, table_outer && array_declarations.array_bounds[table_outer->array_bound].token_count == 0);
        }
    }

    CPreprocessResult callback_tokens = c_preprocess(arguments->arena,
                                                     S8("typedef int (*Callback)"
                                                        "(int value, void *context);\n"
                                                        "int (*rows)[4];\n"),
                                                     (CPreprocessOptions){0});
    CParseResult callback_declarations = c_parse(arguments->arena, callback_tokens);
    BUSTER_TEST(arguments, callback_declarations.diagnostic_count == 0);
    BUSTER_TEST(arguments, callback_declarations.declaration_count == 2);
    if (callback_declarations.declaration_count == 2)
    {
        BUSTER_STRING_TEST(arguments, callback_declarations.declarations[0].name, S8("Callback"));
        BUSTER_TEST(arguments, callback_declarations.declarations[0].kind == C_DECLARATION_TYPEDEF);
        CType* callback_pointer = c_type_from_id(&callback_declarations, callback_declarations.declarations[0].type);
        CType* callback_function = callback_pointer ? c_type_from_id(&callback_declarations, callback_pointer->element_type) : 0;
        BUSTER_TEST(arguments, callback_pointer && callback_pointer->kind == C_TYPE_POINTER);
        BUSTER_TEST(arguments, callback_function && callback_function->kind == C_TYPE_FUNCTION);
        BUSTER_TEST(arguments, callback_function && callback_function->parameter_count == 2);
        if (callback_function && callback_function->parameter_count == 2)
        {
            CType* context_pointer = c_type_from_id(&callback_declarations, callback_declarations.parameters[callback_function->parameter_start + 1].type);
            CType* context_element = context_pointer ? c_type_from_id(&callback_declarations, context_pointer->element_type) : 0;
            BUSTER_TEST(arguments, context_pointer && context_pointer->kind == C_TYPE_POINTER);
            BUSTER_TEST(arguments, context_element && context_element->kind == C_TYPE_VOID);
        }
        BUSTER_STRING_TEST(arguments, callback_declarations.declarations[1].name, S8("rows"));
        CType* rows_pointer = c_type_from_id(&callback_declarations, callback_declarations.declarations[1].type);
        CType* rows_array = rows_pointer ? c_type_from_id(&callback_declarations, rows_pointer->element_type) : 0;
        CType* rows_element = rows_array ? c_type_from_id(&callback_declarations, rows_array->element_type) : 0;
        BUSTER_TEST(arguments, rows_pointer && rows_pointer->kind == C_TYPE_POINTER);
        BUSTER_TEST(arguments, rows_array && rows_array->kind == C_TYPE_ARRAY);
        BUSTER_TEST(arguments, rows_element && rows_element->kind == C_TYPE_INT);
    }

    CPreprocessResult typedef_name_callback_tokens = c_preprocess(arguments->arena,
                                                                  S8("typedef int CallbackResult;\n"
                                                                     "typedef CallbackResult (*AliasCallback)(int value);\n"
                                                                     "AliasCallback callback;\n"),
                                                                  (CPreprocessOptions){0});
    CParserResult typedef_name_callback_syntax = c_parse_ast(arguments->arena, typedef_name_callback_tokens);
    BUSTER_TEST(arguments, typedef_name_callback_syntax.diagnostic_count == 0);
    BUSTER_TEST(arguments, typedef_name_callback_syntax.declaration_count == 3);
    if (typedef_name_callback_syntax.declaration_count == 3)
    {
        CParserDeclaration* alias_callback_syntax = typedef_name_callback_syntax.first_declaration;
        alias_callback_syntax = alias_callback_syntax ? alias_callback_syntax->next : 0;
        BUSTER_TEST(arguments, alias_callback_syntax && alias_callback_syntax->name_token < typedef_name_callback_tokens.token_count);
        if (alias_callback_syntax && alias_callback_syntax->name_token < typedef_name_callback_tokens.token_count)
        {
            BUSTER_STRING_TEST(arguments, typedef_name_callback_tokens.tokens[alias_callback_syntax->name_token].spelling, S8("AliasCallback"));
        }
    }
    CParseResult typedef_name_callback_declarations = c_parse(arguments->arena, typedef_name_callback_tokens);
    BUSTER_TEST(arguments, typedef_name_callback_declarations.diagnostic_count == 0);
    BUSTER_TEST(arguments, typedef_name_callback_declarations.declaration_count == 3);
    if (typedef_name_callback_declarations.declaration_count == 3)
    {
        BUSTER_STRING_TEST(arguments, typedef_name_callback_declarations.declarations[1].name, S8("AliasCallback"));
        BUSTER_TEST(arguments, typedef_name_callback_declarations.declarations[1].kind == C_DECLARATION_TYPEDEF);
        CType* alias_callback_pointer = c_type_from_id(&typedef_name_callback_declarations, typedef_name_callback_declarations.declarations[1].type);
        CType* alias_callback_function = alias_callback_pointer ? c_type_from_id(&typedef_name_callback_declarations, alias_callback_pointer->element_type) : 0;
        BUSTER_TEST(arguments, alias_callback_pointer && alias_callback_pointer->kind == C_TYPE_POINTER);
        BUSTER_TEST(arguments, alias_callback_function && alias_callback_function->kind == C_TYPE_FUNCTION);
        BUSTER_STRING_TEST(arguments, typedef_name_callback_declarations.declarations[2].name, S8("callback"));
    }

    CPreprocessResult qualified_callback_tokens = c_preprocess(arguments->arena,
                                                               S8("typedef struct QualifiedContext"
                                                                  " { int value; } QualifiedContext;\n"
                                                                  "typedef void *"
                                                                  "(*PointerReturningCallback)"
                                                                  "(void *context);\n"
                                                                  "int qualified_parameters("
                                                                  "const QualifiedContext *left,"
                                                                  " QualifiedContext const *right);\n"),
                                                               (CPreprocessOptions){0});
    CParseResult qualified_callback_declarations = c_parse(arguments->arena, qualified_callback_tokens);
    BUSTER_TEST(arguments, qualified_callback_declarations.diagnostic_count == 0);
    BUSTER_TEST(arguments, qualified_callback_declarations.declaration_count == 3);
    if (qualified_callback_declarations.declaration_count == 3)
    {
        CType* callback_pointer = c_type_from_id(&qualified_callback_declarations, qualified_callback_declarations.declarations[1].type);
        CType* callback_function = callback_pointer ? c_type_from_id(&qualified_callback_declarations, callback_pointer->element_type) : 0;
        CType* callback_return = callback_function ? c_type_from_id(&qualified_callback_declarations, callback_function->return_type) : 0;
        CType* callback_return_element = callback_return ? c_type_from_id(&qualified_callback_declarations, callback_return->element_type) : 0;
        BUSTER_TEST(arguments, callback_pointer && callback_pointer->kind == C_TYPE_POINTER);
        BUSTER_TEST(arguments, callback_function && callback_function->kind == C_TYPE_FUNCTION);
        BUSTER_TEST(arguments, callback_return && callback_return->kind == C_TYPE_POINTER);
        BUSTER_TEST(arguments, callback_return_element && callback_return_element->kind == C_TYPE_VOID);

        CType* qualified_function = c_type_from_id(&qualified_callback_declarations, qualified_callback_declarations.declarations[2].type);
        BUSTER_TEST(arguments, qualified_function && qualified_function->kind == C_TYPE_FUNCTION);
        BUSTER_TEST(arguments, qualified_function && qualified_function->parameter_count == 2);
        if (qualified_function && qualified_function->parameter_count == 2)
        {
            for (u32 parameter_index = 0; parameter_index < 2; parameter_index += 1)
            {
                CType* pointer = c_type_from_id(&qualified_callback_declarations,
                                                qualified_callback_declarations.parameters[qualified_function->parameter_start + parameter_index].type);
                CType* element = pointer ? c_type_from_id(&qualified_callback_declarations, pointer->element_type) : 0;
                BUSTER_TEST(arguments, pointer && pointer->kind == C_TYPE_POINTER);
                BUSTER_TEST(arguments, element && element->kind == C_TYPE_STRUCT && element->is_const);
            }
        }
    }

    CPreprocessResult redeclaration_tokens = c_preprocess(arguments->arena,
                                                          S8("int add(int, int);\n"
                                                             "int add(int left, int right)"
                                                             " { return left + right; }\n"),
                                                          (CPreprocessOptions){0});
    CParseResult redeclarations = c_parse(arguments->arena, redeclaration_tokens);
    BUSTER_TEST(arguments, redeclarations.diagnostic_count == 0);
    BUSTER_TEST(arguments, redeclarations.declaration_count == 2);
    BUSTER_TEST(arguments, redeclarations.scopes[0].entity_count == 1);
    BUSTER_TEST(arguments, redeclarations.declaration_count == 2 && redeclarations.declarations[0].entity.value == redeclarations.declarations[1].entity.value);
    BUSTER_TEST(arguments, redeclarations.scopes[0].entity_count == 1 && redeclarations.entities[0].is_definition);

    CPreprocessResult conflicting_tokens = c_preprocess(arguments->arena, S8("int value;\nlong value;\n"), (CPreprocessOptions){0});
    CParseResult conflicting = c_parse(arguments->arena, conflicting_tokens);
    BUSTER_TEST(arguments, conflicting.diagnostic_count == 1);
    BUSTER_TEST(arguments, conflicting.diagnostic_count == 1 && conflicting.diagnostics[0].kind == C_DIAGNOSTIC_CONFLICTING_DECLARATION);
    BUSTER_TEST(arguments, conflicting.entity_count == 1);

    CPreprocessResult overload_tokens = c_preprocess(arguments->arena,
                                                     S8("int select_value(int value)"
                                                        " __asm__(\"select_signed\");\n"
                                                        "int select_value(unsigned value)"
                                                        " __attribute__((__overloadable__))"
                                                        " __asm__(\"select_unsigned\");\n"
                                                        "int select_both(void) {\n"
                                                        "    return select_value(1) +"
                                                        " select_value(1U);\n"
                                                        "}\n"),
                                                     (CPreprocessOptions){0});
    CParseResult overload_parse = c_parse(arguments->arena, overload_tokens);
    BUSTER_TEST(arguments, overload_parse.diagnostic_count == 0);
    BUSTER_TEST(arguments, overload_parse.declaration_count == 3);
    BUSTER_TEST(arguments, overload_parse.entity_count >= 3);
    if (overload_parse.declaration_count == 3)
    {
        BUSTER_TEST(arguments, overload_parse.declarations[0].entity.value != overload_parse.declarations[1].entity.value);
    }
    CIRLowerResult overload_ir = c_lower_to_ir(arguments->arena, S8("overload.c"), overload_tokens, overload_parse, target_native);
    BUSTER_TEST(arguments, overload_ir.diagnostic_count == 0);
    if (overload_ir.program)
    {
        bool found_signed_call = false;
        bool found_unsigned_call = false;
        IrModule* module = &overload_ir.program->modules[0];
        for (u32 function_index = 0; function_index < module->function_count; function_index += 1)
        {
            IrFunction* function = &module->functions[function_index];
            for (u32 instruction_index = 0; instruction_index < function->instruction_count; instruction_index += 1)
            {
                IrInstruction instruction = function->instructions[instruction_index];
                if (instruction.opcode != IR_OPCODE_CALL)
                {
                    continue;
                }
                IrSymbol* symbol = ir_symbol_from_id(&overload_ir.program->symbols, instruction.symbol);
                found_signed_call |= symbol && string_equal(symbol->link_name, S8("select_signed"));
                found_unsigned_call |= symbol && string_equal(symbol->link_name, S8("select_unsigned"));
            }
        }
        BUSTER_TEST(arguments, found_signed_call);
        BUSTER_TEST(arguments, found_unsigned_call);
        IrValidationResult validation = ir_validate_canonical_module(overload_ir.program, module);
        BUSTER_TEST(arguments, validation.error == IR_VALIDATION_NONE);
    }

    CPreprocessResult redefinition_tokens = c_preprocess(arguments->arena,
                                                         S8("int value = 1;\n"
                                                            "int value = 2;\n"),
                                                         (CPreprocessOptions){0});
    CParseResult redefinition = c_parse(arguments->arena, redefinition_tokens);
    BUSTER_TEST(arguments, redefinition.diagnostic_count == 1);
    BUSTER_TEST(arguments, redefinition.diagnostic_count == 1 && redefinition.diagnostics[0].kind == C_DIAGNOSTIC_REDEFINITION);
    BUSTER_TEST(arguments, redefinition.entity_count == 1);

    CPreprocessResult ir_tokens = c_preprocess(arguments->arena, S8("int main(void) { return 1 + 2 * 3; }\n"), (CPreprocessOptions){0});
    CParseResult ir_parse = c_parse(arguments->arena, ir_tokens);
    CIRLowerResult c_ir = c_lower_to_ir(arguments->arena, S8("test.c"), ir_tokens, ir_parse, target_native);
    BUSTER_TEST(arguments, c_ir.diagnostic_count == 0);
    BUSTER_TEST(arguments, c_ir.program != 0);
    if (c_ir.program)
    {
        BUSTER_TEST(arguments, c_ir.program->module_count == 1);
        BUSTER_TEST(arguments, c_ir.program->types.count >= C_TYPE_LONG_DOUBLE);
        BUSTER_TEST(arguments, c_ir.program->symbols.count == 1);
        BUSTER_TEST(arguments, c_ir.program->modules[0].function_count == 1);
        IrFunction* function = c_ir.program->modules[0].functions;
        BUSTER_TEST(arguments, function->state == IR_FUNCTION_LOWERED);
        BUSTER_TEST(arguments, function->block_count == 1);
        BUSTER_TEST(arguments, function->instruction_count == 6);
        BUSTER_TEST(arguments, function->instructions[0].opcode == IR_OPCODE_CONSTANT_INTEGER);
        BUSTER_TEST(arguments, function->instructions[0].immediates[0] == 1);
        BUSTER_TEST(arguments, function->instructions[3].opcode == IR_OPCODE_BINARY);
        BUSTER_TEST(arguments, function->instructions[3].binary_operation == IR_BINARY_INTEGER_MULTIPLY);
        BUSTER_TEST(arguments, function->instructions[4].binary_operation == IR_BINARY_INTEGER_ADD);
        BUSTER_TEST(arguments, function->instructions[5].opcode == IR_OPCODE_RETURN);
        BUSTER_TEST(arguments, function->values[0].canonical_type.value != IR_ID_UNDERLYING_INVALID);
        IrValidationResult validation = ir_validate_canonical_module(c_ir.program, &c_ir.program->modules[0]);
        BUSTER_TEST(arguments, validation.error == IR_VALIDATION_NONE);
    }
    CPreprocessResult nullability_tokens = c_preprocess(arguments->arena,
                                                        S8("void *select_pointer("
                                                           "const void * _Nonnull first,"
                                                           " void * _Nullable second,"
                                                           " void * _Null_unspecified third,"
                                                           " int (*_Null_unspecified callback)"
                                                           "(void * _Nonnull,"
                                                           " char * _Nullable)) {\n"
                                                           "    return second ? second :"
                                                           " (third ? third : (void *)first);\n"
                                                           "}\n"),
                                                        (CPreprocessOptions){0});
    CParseResult nullability_parse = c_parse(arguments->arena, nullability_tokens);
    BUSTER_TEST(arguments, nullability_parse.diagnostic_count == 0);
    BUSTER_TEST(arguments, nullability_parse.declaration_count == 1);
    BUSTER_TEST(arguments, nullability_parse.parameter_count == 6);
    if (nullability_parse.parameter_count == 6)
    {
        BUSTER_STRING_TEST(arguments, nullability_parse.parameters[0].name, S8("first"));
        BUSTER_STRING_TEST(arguments, nullability_parse.parameters[1].name, S8("second"));
        BUSTER_STRING_TEST(arguments, nullability_parse.parameters[2].name, S8("third"));
        BUSTER_STRING_TEST(arguments, nullability_parse.parameters[3].name, S8("callback"));
    }
    CIRLowerResult nullability_ir = c_lower_to_ir(arguments->arena, S8("nullability.c"), nullability_tokens, nullability_parse, target_native);
    BUSTER_TEST(arguments, nullability_ir.diagnostic_count == 0);
    if (nullability_ir.program)
    {
        IrValidationResult validation = ir_validate_canonical_module(nullability_ir.program, &nullability_ir.program->modules[0]);
        BUSTER_TEST(arguments, validation.error == IR_VALIDATION_NONE);
    }
    CPreprocessResult attributed_enum_tokens = c_preprocess(arguments->arena,
                                                            S8("enum NativeStrategy {\n"
                                                               "    NATIVE_SEAMLESS = 0,\n"
                                                               "    NATIVE_ALWAYS = 1\n"
                                                               "} __attribute__((availability("
                                                               "android, strict, introduced = 31)));\n"
                                                               "static inline int clear_rate(void) {\n"
                                                               "    return NATIVE_SEAMLESS;\n"
                                                               "}\n"),
                                                            (CPreprocessOptions){0});
    CParseResult attributed_enum_parse = c_parse(arguments->arena, attributed_enum_tokens);
    BUSTER_TEST(arguments, attributed_enum_parse.diagnostic_count == 0);
    CEntityId attributed_enumerator = c_parse_lookup_entity(&attributed_enum_parse, (CScopeId){.value = 0}, S8("NATIVE_SEAMLESS"));
    BUSTER_TEST(arguments, attributed_enumerator.value != C_ID_UNDERLYING_INVALID);
    if (attributed_enumerator.value != C_ID_UNDERLYING_INVALID)
    {
        BUSTER_TEST(arguments, attributed_enum_parse.entities[attributed_enumerator.value].kind == C_ENTITY_ENUMERATOR);
    }
    CPreprocessResult aggregate_local_tokens = c_preprocess(arguments->arena,
                                                            S8("typedef unsigned long u64;\n"
                                                               "typedef unsigned char u8;\n"
                                                               "struct Inner { u64 x; };\n"
                                                               "struct Outer {"
                                                               " u64 first;"
                                                               " struct Inner inner;"
                                                               " u8 reserved[4];"
                                                               "};\n"
                                                               "struct View {"
                                                               " u8 *pointer;"
                                                               " u64 length;"
                                                               "};\n"
                                                               "int fill(struct Outer *out,"
                                                               " _Bool offset) {\n"
                                                               " u8 local[3];\n"
                                                               " _Static_assert("
                                                               "sizeof(local) == 3, \"array\");\n"
                                                               " struct Outer value ="
                                                               " { .first = 7 };\n"
                                                               " struct View view = {"
                                                               " (u8 *)out + offset, 1 };\n"
                                                               " *out = value;\n"
                                                               " return (int)("
                                                               "sizeof(out->inner.x) +"
                                                               " sizeof(local[0]) +"
                                                               " (view.pointer != (u8 *)out));\n"
                                                               "}\n"),
                                                            (CPreprocessOptions){0});
    CParseResult aggregate_local_parse = c_parse(arguments->arena, aggregate_local_tokens);
    CIRLowerResult aggregate_local_ir = c_lower_to_ir(arguments->arena, S8("aggregate-local.c"), aggregate_local_tokens, aggregate_local_parse, target_native);
    BUSTER_TEST(arguments, aggregate_local_parse.diagnostic_count == 0);
    BUSTER_TEST(arguments, aggregate_local_ir.diagnostic_count == 0);
    BUSTER_TEST(arguments, aggregate_local_ir.program != 0);
    if (aggregate_local_ir.program)
    {
        IrModule* aggregate_local_module = &aggregate_local_ir.program->modules[0];
        BUSTER_TEST(arguments, aggregate_local_module->function_count == 1);
        IrFunction* fill = aggregate_local_module->functions;
        BUSTER_TEST(arguments, fill->state == IR_FUNCTION_LOWERED);
        u32 aggregate_count = 0;
        u32 store_count = 0;
        for (u32 instruction_index = 0; instruction_index < fill->instruction_count; instruction_index += 1)
        {
            IrOpcode opcode = fill->instructions[instruction_index].opcode;
            aggregate_count += opcode == IR_OPCODE_AGGREGATE || opcode == IR_OPCODE_ARRAY;
            store_count += opcode == IR_OPCODE_STORE;
        }
        BUSTER_TEST(arguments, aggregate_count >= 3);
        BUSTER_TEST(arguments, store_count >= 2);
        IrValidationResult validation = ir_validate_canonical_module(aggregate_local_ir.program, aggregate_local_module);
        BUSTER_TEST(arguments, validation.error == IR_VALIDATION_NONE);
    }
    CPreprocessResult multi_for_tokens = c_preprocess(arguments->arena,
                                                      S8("int sum(void) {\n"
                                                         " int total = 0;\n"
                                                         " for (unsigned long i = 0,"
                                                         " reverse_i = 5;"
                                                         " i < reverse_i;"
                                                         " i += 1, reverse_i -= 1) {\n"
                                                         "  total += (int)(i + reverse_i);\n"
                                                         " }\n"
                                                         " return total;\n"
                                                         "}\n"),
                                                      (CPreprocessOptions){0});
    CParseResult multi_for_parse = c_parse(arguments->arena, multi_for_tokens);
    CIRLowerResult multi_for_ir = c_lower_to_ir(arguments->arena, S8("multi-for.c"), multi_for_tokens, multi_for_parse, target_native);
    BUSTER_TEST(arguments, multi_for_parse.diagnostic_count == 0);
    BUSTER_TEST(arguments, multi_for_ir.diagnostic_count == 0);
    BUSTER_TEST(arguments, multi_for_ir.program != 0);
    if (multi_for_ir.program)
    {
        IrModule* multi_for_module = &multi_for_ir.program->modules[0];
        IrFunction* sum = multi_for_module->functions;
        BUSTER_TEST(arguments, multi_for_module->function_count == 1);
        BUSTER_TEST(arguments, sum->state == IR_FUNCTION_LOWERED);
        BUSTER_TEST(arguments, sum->local_count == 3);
        BUSTER_TEST(arguments, sum->block_count >= 4);
        BUSTER_TEST(arguments, ir_validate_canonical_module(multi_for_ir.program, multi_for_module).error == IR_VALIDATION_NONE);
    }
    CPreprocessResult math_builtin_tokens = c_preprocess(arguments->arena,
                                                         S8("float lower(float value) {\n"
                                                            " return __builtin_floorf(value);\n"
                                                            "}\n"
                                                            "double power(double left,"
                                                            " double right) {\n"
                                                            " return __builtin_pow(left, right);\n"
                                                            "}\n"),
                                                         (CPreprocessOptions){0});
    CParseResult math_builtin_parse = c_parse(arguments->arena, math_builtin_tokens);
    CIRLowerResult math_builtin_ir = c_lower_to_ir(arguments->arena, S8("math-builtins.c"), math_builtin_tokens, math_builtin_parse, target_native);
    BUSTER_TEST(arguments, math_builtin_parse.diagnostic_count == 0);
    BUSTER_TEST(arguments, math_builtin_ir.diagnostic_count == 0);
    BUSTER_TEST(arguments, math_builtin_ir.program != 0);
    if (math_builtin_ir.program)
    {
        IrModule* math_module = &math_builtin_ir.program->modules[0];
        u32 call_count = 0;
        for (u32 function_index = 0; function_index < math_module->function_count; function_index += 1)
        {
            IrFunction* function = &math_module->functions[function_index];
            BUSTER_TEST(arguments, function->state == IR_FUNCTION_LOWERED);
            for (u32 instruction_index = 0; instruction_index < function->instruction_count; instruction_index += 1)
            {
                call_count += function->instructions[instruction_index].opcode == IR_OPCODE_CALL;
            }
        }
        bool found_floorf = false;
        bool found_pow = false;
        for (u32 symbol_index = 0; symbol_index < math_builtin_ir.program->symbols.count; symbol_index += 1)
        {
            IrSymbol* symbol = &math_builtin_ir.program->symbols.symbols[symbol_index];
            if (symbol->kind != IR_SYMBOL_FUNCTION || symbol->linkage != IR_LINKAGE_IMPORT)
            {
                continue;
            }
            found_floorf |= string_equal(symbol->link_name, S8("floorf"));
            found_pow |= string_equal(symbol->link_name, S8("pow"));
        }
        BUSTER_TEST(arguments, call_count == 2);
        BUSTER_TEST(arguments, found_floorf);
        BUSTER_TEST(arguments, found_pow);
        BUSTER_TEST(arguments, ir_validate_canonical_module(math_builtin_ir.program, math_module).error == IR_VALIDATION_NONE);
    }
    TemporalArena global_temporary = scratch_begin(0, 0);
    Arena* global_arena = global_temporary.arena;
    CPreprocessResult global_tokens = c_preprocess(global_arena,
                                                   S8("extern int imported;\n"
                                                      "static int counter;\n"
                                                      "const int base = 6;\n"
                                                      "const int answer = base * (8 - 1);\n"
                                                      "double ratio = -2.5;\n"
                                                      "static const char label[] = \"ok\\n\";\n"
                                                      "int address_target = 7;\n"
                                                      "int *address_pointer = &address_target;\n"
                                                      "int *null_pointer = 0;\n"
                                                      "int use(void)"
                                                      " { counter += answer;"
                                                      " return counter + answer + imported + '\\n'; }\n"
                                                      "char *text(void) { return \"hi\"; }\n"),
                                                   (CPreprocessOptions){0});
    CParseResult global_parse = c_parse(global_arena, global_tokens);
    CIRLowerResult global_ir = c_lower_to_ir(global_arena, S8("globals.c"), global_tokens, global_parse, target_native);
    BUSTER_TEST(arguments, global_parse.diagnostic_count == 0);
    BUSTER_TEST(arguments, global_ir.diagnostic_count == 0);
    BUSTER_TEST(arguments, global_ir.program != 0);
    if (global_ir.program)
    {
        IrModule* global_module = &global_ir.program->modules[0];
        BUSTER_TEST(arguments, global_ir.program->symbols.count == 12);
        BUSTER_TEST(arguments, global_module->global_count == 9);
        BUSTER_TEST(arguments, global_module->function_count == 2);
        BUSTER_TEST(arguments, global_module->globals[0].initializer_kind == IR_GLOBAL_INITIALIZER_ZERO);
        BUSTER_TEST(arguments, global_module->globals[1].initializer_kind == IR_GLOBAL_INITIALIZER_INTEGER);
        BUSTER_TEST(arguments, global_module->globals[1].initializer_bits == 6);
        BUSTER_TEST(arguments, global_module->globals[1].is_read_only);
        BUSTER_TEST(arguments, global_module->globals[2].initializer_kind == IR_GLOBAL_INITIALIZER_INTEGER);
        BUSTER_TEST(arguments, global_module->globals[2].initializer_bits == 42);
        BUSTER_TEST(arguments, global_module->globals[2].is_read_only);
        BUSTER_TEST(arguments, global_module->globals[3].initializer_kind == IR_GLOBAL_INITIALIZER_FLOAT);
        BUSTER_TEST(arguments, global_module->globals[4].initializer_kind == IR_GLOBAL_INITIALIZER_BYTES);
        BUSTER_TEST(arguments, global_module->globals[4].bytes.length == 4);
        BUSTER_TEST(arguments, global_module->globals[4].bytes.pointer[0] == 'o' && global_module->globals[4].bytes.pointer[1] == 'k' &&
                                   global_module->globals[4].bytes.pointer[2] == '\n' && global_module->globals[4].bytes.pointer[3] == 0);
        BUSTER_TEST(arguments, global_module->globals[5].initializer_kind == IR_GLOBAL_INITIALIZER_INTEGER);
        BUSTER_TEST(arguments, global_module->globals[6].initializer_kind == IR_GLOBAL_INITIALIZER_SYMBOL_ADDRESS);
        BUSTER_TEST(arguments, global_module->globals[6].initializer_symbol.value == global_module->globals[5].symbol.value);
        BUSTER_TEST(arguments, global_module->globals[7].initializer_kind == IR_GLOBAL_INITIALIZER_ZERO);
        BUSTER_TEST(arguments, global_module->globals[8].initializer_kind == IR_GLOBAL_INITIALIZER_BYTES);
        IrSymbol* imported_symbol = ir_symbol_from_id(&global_ir.program->symbols, (IrSymbolId){.value = 0});
        IrSymbol* counter_symbol = ir_symbol_from_id(&global_ir.program->symbols, global_module->globals[0].symbol);
        BUSTER_TEST(arguments, imported_symbol && imported_symbol->kind == IR_SYMBOL_DATA && imported_symbol->linkage == IR_LINKAGE_IMPORT &&
                                   !imported_symbol->is_definition);
        BUSTER_TEST(arguments, counter_symbol && counter_symbol->linkage == IR_LINKAGE_INTERNAL);
        u32 global_reference_count = 0;
        IrFunction* use = global_module->functions;
        for (u32 instruction_index = 0; instruction_index < use->instruction_count; instruction_index += 1)
        {
            global_reference_count += use->instructions[instruction_index].opcode == IR_OPCODE_GLOBAL;
        }
        BUSTER_TEST(arguments, global_reference_count == 5);
        IrFunction* text = global_module->functions + 1;
        u32 index_count = 0;
        u32 address_count = 0;
        for (u32 instruction_index = 0; instruction_index < text->instruction_count; instruction_index += 1)
        {
            index_count += text->instructions[instruction_index].opcode == IR_OPCODE_INDEX;
            address_count += text->instructions[instruction_index].opcode == IR_OPCODE_ADDRESS_OF;
        }
        BUSTER_TEST(arguments, index_count == 1);
        BUSTER_TEST(arguments, address_count == 1);
        IrValidationResult validation = ir_validate_canonical_module(global_ir.program, global_module);
        BUSTER_TEST(arguments, validation.error == IR_VALIDATION_NONE);
    }
    CPreprocessResult forward_tokens = c_preprocess(global_arena,
                                                    S8("extern int later;\n"
                                                       "int *before = &later;\n"
                                                       "int later = 9;\n"),
                                                    (CPreprocessOptions){0});
    CParseResult forward_parse = c_parse(global_arena, forward_tokens);
    CIRLowerResult forward_ir = c_lower_to_ir(global_arena, S8("forward-globals.c"), forward_tokens, forward_parse, target_native);
    BUSTER_TEST(arguments, forward_parse.diagnostic_count == 0);
    BUSTER_TEST(arguments, forward_ir.diagnostic_count == 0);
    BUSTER_TEST(arguments, forward_ir.program != 0);
    if (forward_ir.program)
    {
        IrModule* forward_module = &forward_ir.program->modules[0];
        BUSTER_TEST(arguments, forward_module->global_count == 2);
        if (forward_module->global_count == 2)
        {
            BUSTER_TEST(arguments, forward_module->globals[0].initializer_kind == IR_GLOBAL_INITIALIZER_INTEGER);
            BUSTER_TEST(arguments, forward_module->globals[1].initializer_kind == IR_GLOBAL_INITIALIZER_SYMBOL_ADDRESS);
            BUSTER_TEST(arguments, forward_module->globals[1].initializer_symbol.value == forward_module->globals[0].symbol.value);
        }
    }
    scratch_end(global_temporary);
    CPreprocessResult void_tokens = c_preprocess(arguments->arena, S8("void reset(void) { return; }\n"), (CPreprocessOptions){0});
    CParseResult void_parse = c_parse(arguments->arena, void_tokens);
    CIRLowerResult void_ir = c_lower_to_ir(arguments->arena, S8("void.c"), void_tokens, void_parse, target_native);
    BUSTER_TEST(arguments, void_ir.diagnostic_count == 0);
    if (void_ir.program)
    {
        IrFunction* function = void_ir.program->modules[0].functions;
        IrType* function_type = ir_type_from_id(&void_ir.program->types, function->canonical_type);
        IrType* return_type = function_type ? ir_type_from_id(&void_ir.program->types, function_type->return_type) : 0;
        BUSTER_TEST(arguments, function->state == IR_FUNCTION_LOWERED);
        BUSTER_TEST(arguments, return_type && return_type->kind == IR_TYPE_VOID);
        BUSTER_TEST(arguments, function->instruction_count == 1);
        BUSTER_TEST(arguments, function->instructions[0].opcode == IR_OPCODE_RETURN);
        BUSTER_TEST(arguments, function->instructions[0].operand_count == 0);
        IrValidationResult validation = ir_validate_canonical_module(void_ir.program, &void_ir.program->modules[0]);
        BUSTER_TEST(arguments, validation.error == IR_VALIDATION_NONE);
    }
    Arena* scalar_conflicts[] = {
        arguments->arena,
    };
    TemporalArena scalar_temporary = scratch_begin(scalar_conflicts, BUSTER_ARRAY_LENGTH(scalar_conflicts));
    Arena* scalar_arena = scalar_temporary.arena;
    CPreprocessResult scalar_tokens = c_preprocess(scalar_arena,
                                                   S8("long widen(long value);\n"
                                                      "unsigned short narrow("
                                                      "unsigned char value);\n"
                                                      "double blend(float left,"
                                                      " double right);\n"),
                                                   (CPreprocessOptions){0});
    CParseResult scalar_parse = c_parse(scalar_arena, scalar_tokens);
    Target lp64_target = target_native;
    lp64_target.os = OPERATING_SYSTEM_LINUX;
    CIRLowerResult lp64_ir = c_lower_to_ir(scalar_arena, S8("scalar-lp64.c"), scalar_tokens, scalar_parse, lp64_target);
    BUSTER_TEST(arguments, lp64_ir.diagnostic_count == 0);
    if (lp64_ir.program)
    {
        IrFunction* functions = lp64_ir.program->modules[0].functions;
        IrType* widen_type = ir_type_from_id(&lp64_ir.program->types, functions[0].canonical_type);
        IrType* widen_return = ir_type_from_id(&lp64_ir.program->types, widen_type->return_type);
        IrType* widen_parameter = ir_type_from_id(&lp64_ir.program->types, widen_type->parameter_types[0]);
        BUSTER_TEST(arguments, widen_return->kind == IR_TYPE_INTEGER);
        BUSTER_TEST(arguments, widen_return->bit_width == 64);
        BUSTER_TEST(arguments, widen_return->is_signed);
        BUSTER_TEST(arguments, widen_parameter->bit_width == 64);
        IrType* narrow_type = ir_type_from_id(&lp64_ir.program->types, functions[1].canonical_type);
        IrType* narrow_return = ir_type_from_id(&lp64_ir.program->types, narrow_type->return_type);
        IrType* narrow_parameter = ir_type_from_id(&lp64_ir.program->types, narrow_type->parameter_types[0]);
        BUSTER_TEST(arguments, narrow_return->bit_width == 16);
        BUSTER_TEST(arguments, !narrow_return->is_signed);
        BUSTER_TEST(arguments, narrow_parameter->bit_width == 8);
        BUSTER_TEST(arguments, !narrow_parameter->is_signed);
        IrType* blend_type = ir_type_from_id(&lp64_ir.program->types, functions[2].canonical_type);
        IrType* blend_return = ir_type_from_id(&lp64_ir.program->types, blend_type->return_type);
        IrType* blend_left = ir_type_from_id(&lp64_ir.program->types, blend_type->parameter_types[0]);
        IrType* blend_right = ir_type_from_id(&lp64_ir.program->types, blend_type->parameter_types[1]);
        BUSTER_TEST(arguments, blend_return->kind == IR_TYPE_FLOAT);
        BUSTER_TEST(arguments, blend_return->bit_width == 64);
        BUSTER_TEST(arguments, blend_left->bit_width == 32);
        BUSTER_TEST(arguments, blend_right->bit_width == 64);
    }
    Target llp64_target = target_native;
    llp64_target.os = OPERATING_SYSTEM_WINDOWS;
    CIRLowerResult llp64_ir = c_lower_to_ir(scalar_arena, S8("scalar-llp64.c"), scalar_tokens, scalar_parse, llp64_target);
    BUSTER_TEST(arguments, llp64_ir.diagnostic_count == 0);
    if (llp64_ir.program)
    {
        IrFunction* widen = llp64_ir.program->modules[0].functions;
        IrType* function_type = ir_type_from_id(&llp64_ir.program->types, widen->canonical_type);
        IrType* return_type = ir_type_from_id(&llp64_ir.program->types, function_type->return_type);
        IrType* parameter_type = ir_type_from_id(&llp64_ir.program->types, function_type->parameter_types[0]);
        BUSTER_TEST(arguments, return_type->bit_width == 32);
        BUSTER_TEST(arguments, parameter_type->bit_width == 32);
    }
    CPreprocessResult integer_body_tokens = c_preprocess(scalar_arena,
                                                         S8("unsigned long combine("
                                                            "unsigned short left,"
                                                            " unsigned long right)\n"
                                                            "{\n"
                                                            "    unsigned char narrow"
                                                            " = left;\n"
                                                            "    unsigned long widened"
                                                            " = narrow;\n"
                                                            "    return widened / right;\n"
                                                            "}\n"),
                                                         (CPreprocessOptions){0});
    CParseResult integer_body_parse = c_parse(scalar_arena, integer_body_tokens);
    CIRLowerResult integer_body_ir = c_lower_to_ir(scalar_arena, S8("integer-body.c"), integer_body_tokens, integer_body_parse, lp64_target);
    BUSTER_TEST(arguments, integer_body_ir.diagnostic_count == 0);
    if (integer_body_ir.program)
    {
        IrFunction* function = integer_body_ir.program->modules[0].functions;
        u32 truncate_count = 0;
        u32 extend_count = 0;
        u32 unsigned_divide_count = 0;
        for (u32 instruction_index = 0; instruction_index < function->instruction_count; instruction_index += 1)
        {
            IrInstruction* instruction = &function->instructions[instruction_index];
            truncate_count += instruction->opcode == IR_OPCODE_CAST && instruction->conversion_operation == IR_CONVERSION_INTEGER_TRUNCATE;
            extend_count += instruction->opcode == IR_OPCODE_CAST && instruction->conversion_operation == IR_CONVERSION_INTEGER_ZERO_EXTEND;
            unsigned_divide_count += instruction->opcode == IR_OPCODE_BINARY && instruction->binary_operation == IR_BINARY_UNSIGNED_DIVIDE;
        }
        BUSTER_TEST(arguments, function->state == IR_FUNCTION_LOWERED);
        BUSTER_TEST(arguments, truncate_count == 1);
        BUSTER_TEST(arguments, extend_count == 1);
        BUSTER_TEST(arguments, unsigned_divide_count == 1);
        BUSTER_TEST(arguments, ir_validate_canonical_module(integer_body_ir.program, &integer_body_ir.program->modules[0]).error == IR_VALIDATION_NONE);
    }
    CPreprocessResult float_body_tokens = c_preprocess(scalar_arena,
                                                       S8("double blend_values("
                                                          "float left, int count)\n"
                                                          "{\n"
                                                          "    double base = 1.5;\n"
                                                          "    float scale = 2.0f;\n"
                                                          "    return base +"
                                                          " scale * left + count;\n"
                                                          "}\n"),
                                                       (CPreprocessOptions){0});
    CParseResult float_body_parse = c_parse(scalar_arena, float_body_tokens);
    CIRLowerResult float_body_ir = c_lower_to_ir(scalar_arena, S8("float-body.c"), float_body_tokens, float_body_parse, lp64_target);
    BUSTER_TEST(arguments, float_body_ir.diagnostic_count == 0);
    if (float_body_ir.program)
    {
        IrFunction* function = float_body_ir.program->modules[0].functions;
        u32 constant_count = 0;
        u32 multiply_count = 0;
        u32 extend_count = 0;
        u32 integer_to_float_count = 0;
        for (u32 instruction_index = 0; instruction_index < function->instruction_count; instruction_index += 1)
        {
            IrInstruction* instruction = &function->instructions[instruction_index];
            constant_count += instruction->opcode == IR_OPCODE_CONSTANT_FLOAT;
            multiply_count += instruction->opcode == IR_OPCODE_BINARY && instruction->binary_operation == IR_BINARY_FLOAT_MULTIPLY;
            extend_count += instruction->opcode == IR_OPCODE_CAST && instruction->conversion_operation == IR_CONVERSION_FLOAT_EXTEND;
            integer_to_float_count += instruction->opcode == IR_OPCODE_CAST && instruction->conversion_operation == IR_CONVERSION_SIGNED_INTEGER_TO_FLOAT;
        }
        BUSTER_TEST(arguments, function->state == IR_FUNCTION_LOWERED);
        BUSTER_TEST(arguments, constant_count == 2);
        BUSTER_TEST(arguments, multiply_count == 1);
        BUSTER_TEST(arguments, extend_count == 1);
        BUSTER_TEST(arguments, integer_to_float_count == 1);
        BUSTER_TEST(arguments, ir_validate_canonical_module(float_body_ir.program, &float_body_ir.program->modules[0]).error == IR_VALIDATION_NONE);
    }
    CPreprocessResult pointer_body_tokens = c_preprocess(scalar_arena,
                                                         S8("int *identity_pointer("
                                                            "int *value)\n"
                                                            "{ return value; }\n"
                                                            "int *address_value(int value)\n"
                                                            "{ return &value; }\n"
                                                            "int load_value(int *value)\n"
                                                            "{ return *value; }\n"),
                                                         (CPreprocessOptions){0});
    CParseResult pointer_body_parse = c_parse(scalar_arena, pointer_body_tokens);
    CIRLowerResult pointer_body_ir = c_lower_to_ir(scalar_arena, S8("pointer-body.c"), pointer_body_tokens, pointer_body_parse, lp64_target);
    BUSTER_TEST(arguments, pointer_body_ir.diagnostic_count == 0);
    if (pointer_body_ir.program)
    {
        IrModule* module = &pointer_body_ir.program->modules[0];
        BUSTER_TEST(arguments, module->function_count == 3);
        BUSTER_TEST(arguments, module->functions[0].state == IR_FUNCTION_LOWERED);
        BUSTER_TEST(arguments, module->functions[1].state == IR_FUNCTION_LOWERED);
        BUSTER_TEST(arguments, module->functions[2].state == IR_FUNCTION_LOWERED);
        u32 address_count = 0;
        u32 dereference_count = 0;
        for (u32 function_index = 0; function_index < module->function_count; function_index += 1)
        {
            IrFunction* function = &module->functions[function_index];
            for (u32 instruction_index = 0; instruction_index < function->instruction_count; instruction_index += 1)
            {
                address_count += function->instructions[instruction_index].opcode == IR_OPCODE_ADDRESS_OF;
                dereference_count += function->instructions[instruction_index].opcode == IR_OPCODE_DEREFERENCE;
            }
        }
        BUSTER_TEST(arguments, address_count == 1);
        BUSTER_TEST(arguments, dereference_count == 1);
        BUSTER_TEST(arguments, ir_validate_canonical_module(pointer_body_ir.program, module).error == IR_VALIDATION_NONE);
    }
    CPreprocessResult function_pointer_tokens =
        c_preprocess(scalar_arena,
                     S8("extern int launch(void *(*)(void *), void *);\n"
                        "static void *worker(void *argument) { return argument; }\n"
                        "int run(void *argument) { return launch(worker, argument); }\n"
                        "void *invoke(void *argument) { return ((void *(*)(void *))worker)(argument); }\n"),
                     (CPreprocessOptions){0});
    CParseResult function_pointer_parse = c_parse(scalar_arena, function_pointer_tokens);
    CIRLowerResult function_pointer_ir =
        c_lower_to_ir(scalar_arena, S8("function-pointer.c"), function_pointer_tokens, function_pointer_parse, lp64_target);
    BUSTER_TEST(arguments, function_pointer_parse.diagnostic_count == 0);
    BUSTER_TEST(arguments, function_pointer_ir.diagnostic_count == 0);
    if (function_pointer_ir.program)
    {
        IrModule* module = &function_pointer_ir.program->modules[0];
        BUSTER_TEST(arguments, module->function_count == 4);
        if (module->function_count == 4)
        {
            IrType* launch_type = ir_type_from_id(&function_pointer_ir.program->types, module->functions[0].canonical_type);
            IrType* entry_pointer = launch_type ? ir_type_from_id(&function_pointer_ir.program->types, launch_type->parameter_types[0]) : 0;
            IrType* entry_function =
                entry_pointer && entry_pointer->kind == IR_TYPE_POINTER ? ir_type_from_id(&function_pointer_ir.program->types, entry_pointer->element_type) : 0;
            BUSTER_TEST(arguments, entry_pointer && entry_pointer->kind == IR_TYPE_POINTER);
            BUSTER_TEST(arguments, entry_function && entry_function->kind == IR_TYPE_FUNCTION);
            BUSTER_TEST(arguments, module->functions[1].state == IR_FUNCTION_LOWERED);
            BUSTER_TEST(arguments, module->functions[2].state == IR_FUNCTION_LOWERED);
            BUSTER_TEST(arguments, module->functions[3].state == IR_FUNCTION_LOWERED);
        }
    }
    CPreprocessResult typedef_shadow_tokens = c_preprocess(scalar_arena,
                                                           S8("typedef void *id;\n"
                                                              "typedef int TokenId;\n"
                                                              "void tokenize(void) { TokenId id = 0; id = 1; }\n"),
                                                           (CPreprocessOptions){0});
    CParseResult typedef_shadow_parse = c_parse(scalar_arena, typedef_shadow_tokens);
    CIRLowerResult typedef_shadow_ir = c_lower_to_ir(scalar_arena, S8("typedef-shadow.c"), typedef_shadow_tokens, typedef_shadow_parse, lp64_target);
    BUSTER_TEST(arguments, typedef_shadow_parse.diagnostic_count == 0);
    BUSTER_TEST(arguments, typedef_shadow_ir.diagnostic_count == 0);
    if (typedef_shadow_ir.program)
    {
        IrModule* module = &typedef_shadow_ir.program->modules[0];
        BUSTER_TEST(arguments, module->function_count == 1);
        BUSTER_TEST(arguments, module->function_count == 1 && module->functions[0].state == IR_FUNCTION_LOWERED);
    }
    CPreprocessResult array_type_tokens = c_preprocess(scalar_arena, S8("int sum_four(int values[4]);\n"), (CPreprocessOptions){0});
    CParseResult array_type_parse = c_parse(scalar_arena, array_type_tokens);
    CIRLowerResult array_type_ir = c_lower_to_ir(scalar_arena, S8("array-type.c"), array_type_tokens, array_type_parse, lp64_target);
    BUSTER_TEST(arguments, array_type_ir.diagnostic_count == 0);
    if (array_type_ir.program)
    {
        IrFunction* function = array_type_ir.program->modules[0].functions;
        IrType* signature = ir_type_from_id(&array_type_ir.program->types, function->canonical_type);
        IrType* parameter = ir_type_from_id(&array_type_ir.program->types, signature->parameter_types[0]);
        BUSTER_TEST(arguments, parameter->kind == IR_TYPE_POINTER);
        IrType* element = ir_type_from_id(&array_type_ir.program->types, parameter->element_type);
        BUSTER_TEST(arguments, element->kind == IR_TYPE_INTEGER);
        BUSTER_TEST(arguments, element->bit_width == 32);
        bool found_array = false;
        for (u32 type_index = 0; type_index < array_type_ir.program->types.count; type_index += 1)
        {
            IrType* type = &array_type_ir.program->types.types[type_index];
            if (type->kind == IR_TYPE_ARRAY)
            {
                found_array = type->element_count == 4 && type->layout.size == 16 && type->layout.alignment == 4;
            }
        }
        BUSTER_TEST(arguments, found_array);
    }
    scratch_end(scalar_temporary);
    TemporalArena aggregate_temporary = scratch_begin(0, 0);
    CPreprocessResult aggregate_tokens = c_preprocess(aggregate_temporary.arena,
                                                      S8("struct Pair {\n"
                                                         "    int left;\n"
                                                         "    long right;\n"
                                                         "};\n"
                                                         "union Bits {\n"
                                                         "    unsigned int word;\n"
                                                         "    float real;\n"
                                                         "};\n"
                                                         "struct Node {\n"
                                                         "    int value;\n"
                                                         "    struct Node *next;\n"
                                                         "};\n"
                                                         "typedef struct Pair Pair;\n"
                                                         "typedef struct {\n"
                                                         "    int x;\n"
                                                         "    int y;\n"
                                                         "} AnonymousPair;\n"
                                                         "enum Direction {\n"
                                                         "    DIRECTION_NEGATIVE = -1,\n"
                                                         "    DIRECTION_ZERO,\n"
                                                         "    DIRECTION_POSITIVE = 4\n"
                                                         "};\n"
                                                         "static Pair origin ="
                                                         " {.right = 2,"
                                                         " .left = DIRECTION_POSITIVE};\n"
                                                         "static int sequence[3] ="
                                                         " {[2] = 6, [0] = 4, [1] = 5};\n"
                                                         "static union Bits bits = {.word = 7};\n"
                                                         "static int direction ="
                                                         " DIRECTION_POSITIVE + 1;\n"
                                                         "int sum_pair(int left, int right) {\n"
                                                         "    Pair pair;\n"
                                                         "    pair.left = left;\n"
                                                         "    pair.right = right;\n"
                                                         "    pair.left += 1;\n"
                                                         "    return pair.left + pair.right;\n"
                                                         "}\n"
                                                         "int node_value(struct Node *node) {\n"
                                                         "    return node->value;\n"
                                                         "}\n"
                                                         "int next_node_value(struct Node *node) {\n"
                                                         "    return node->next->value;\n"
                                                         "}\n"
                                                         "Pair echo_pair(Pair pair) {\n"
                                                         "    return pair;\n"
                                                         "}\n"
                                                         "int read_pair(Pair pair) {\n"
                                                         "    return pair.left;\n"
                                                         "}\n"
                                                         "int read_echo_pair(Pair pair) {\n"
                                                         "    return echo_pair(pair).left;\n"
                                                         "}\n"
                                                         "int call_read(int left) {\n"
                                                         "    Pair pair;\n"
                                                         "    pair.left = left;\n"
                                                         "    pair.right = 0;\n"
                                                         "    return read_pair(echo_pair(pair));\n"
                                                         "}\n"
                                                         "int anonymous_sum(int x) {\n"
                                                         "    AnonymousPair pair;\n"
                                                         "    pair.x = x;\n"
                                                         "    pair.y = 1;\n"
                                                         "    return pair.x + pair.y;\n"
                                                         "}\n"
                                                         "int enum_value(void) {\n"
                                                         "    return DIRECTION_NEGATIVE"
                                                         " + DIRECTION_ZERO"
                                                         " + DIRECTION_POSITIVE;\n"
                                                         "}\n"
                                                         "int array_sum(int index) {\n"
                                                         "    int values[3];\n"
                                                         "    values[0] = 1;\n"
                                                         "    values[1] = 2;\n"
                                                         "    values[2] = 3;\n"
                                                         "    values[index] += 1;\n"
                                                         "    return values[0] + values[index];\n"
                                                         "}\n"
                                                         "int first(int values[3]) {\n"
                                                         "    return values[0];\n"
                                                         "}\n"
                                                         "int call_first(void) {\n"
                                                         "    int values[3];\n"
                                                         "    values[0] = 7;\n"
                                                         "    return first(values);\n"
                                                         "}\n"
                                                         "int *offset(int *values) {\n"
                                                         "    return values + 1;\n"
                                                         "}\n"
                                                         "int same(int *left, int *right) {\n"
                                                         "    return left == right;\n"
                                                         "}\n"),
                                                      (CPreprocessOptions){0});
    CParseResult aggregate_parse = c_parse(aggregate_temporary.arena, aggregate_tokens);
    BUSTER_TEST(arguments, aggregate_parse.diagnostic_count == 0);
    BUSTER_TEST(arguments, aggregate_parse.declaration_count == 24);
    BUSTER_TEST(arguments, aggregate_parse.member_count == 8);
    BUSTER_TEST(arguments, aggregate_parse.enum_member_count == 3);
    u32 complete_aggregate_count = 0;
    bool found_self_pointer = false;
    for (u32 type_index = 0; type_index < aggregate_parse.type_count; type_index += 1)
    {
        CType* type = &aggregate_parse.types[type_index];
        if ((type->kind == C_TYPE_STRUCT || type->kind == C_TYPE_UNION) && type->is_complete)
        {
            complete_aggregate_count += 1;
            if (string_equal(type->tag, S8("Node")) && type->member_count == 2)
            {
                CMember* next = &aggregate_parse.members[type->member_start + 1];
                CType* pointer = &aggregate_parse.types[next->type.value];
                found_self_pointer = pointer->kind == C_TYPE_POINTER && pointer->element_type.value == type_index;
            }
        }
    }
    BUSTER_TEST(arguments, complete_aggregate_count == 4);
    BUSTER_TEST(arguments, found_self_pointer);
    CIRLowerResult aggregate_ir = c_lower_to_ir(aggregate_temporary.arena, S8("aggregates.c"), aggregate_tokens, aggregate_parse, target_native);
    BUSTER_TEST(arguments, aggregate_ir.diagnostic_count == 0);
    if (aggregate_ir.program)
    {
        bool pair_layout = false;
        bool union_layout = false;
        bool node_layout = false;
        for (u32 type_index = 0; type_index < aggregate_ir.program->types.count; type_index += 1)
        {
            IrType* type = &aggregate_ir.program->types.types[type_index];
            if (type->kind == IR_TYPE_STRUCT && string_equal(type->name, S8("Pair")))
            {
                u64 expected_size = target_native.os == OPERATING_SYSTEM_WINDOWS ? 8 : 16;
                u64 expected_offset = target_native.os == OPERATING_SYSTEM_WINDOWS ? 4 : 8;
                pair_layout =
                    type->layout.resolved && type->layout.size == expected_size && type->field_count == 2 && type->fields[1].offset == expected_offset;
            }
            else if (type->kind == IR_TYPE_UNION && string_equal(type->name, S8("Bits")))
            {
                union_layout =
                    type->layout.resolved && type->layout.size == 4 && type->field_count == 2 && type->fields[0].offset == 0 && type->fields[1].offset == 0;
            }
            else if (type->kind == IR_TYPE_STRUCT && string_equal(type->name, S8("Node")))
            {
                node_layout = type->layout.resolved && type->layout.size == 16 && type->field_count == 2 && type->fields[1].offset == 8;
            }
        }
        BUSTER_TEST(arguments, pair_layout);
        BUSTER_TEST(arguments, union_layout);
        BUSTER_TEST(arguments, node_layout);
        IrModule* module = &aggregate_ir.program->modules[0];
        BUSTER_TEST(arguments, module->global_count == 4);
        if (module->global_count == 4)
        {
            BUSTER_TEST(arguments, module->globals[0].initializer_kind == IR_GLOBAL_INITIALIZER_BYTES);
            BUSTER_TEST(arguments, module->globals[0].bytes.length != 0);
            IrType* origin_type = ir_type_from_id(&aggregate_ir.program->types, module->globals[0].type);
            BUSTER_TEST(arguments,
                        origin_type && module->globals[0].bytes.pointer[0] == 4 && module->globals[0].bytes.pointer[origin_type->fields[1].offset] == 2);
            IrType* sequence_type = ir_type_from_id(&aggregate_ir.program->types, module->globals[1].type);
            IrType* sequence_element = sequence_type ? ir_type_from_id(&aggregate_ir.program->types, sequence_type->element_type) : 0;
            BUSTER_TEST(arguments, sequence_type && sequence_element && module->globals[1].bytes.length == sequence_type->layout.size &&
                                       module->globals[1].bytes.pointer[0] == 4 && module->globals[1].bytes.pointer[sequence_element->layout.size] == 5 &&
                                       module->globals[1].bytes.pointer[sequence_element->layout.size * 2] == 6);
            BUSTER_TEST(arguments, module->globals[2].bytes.pointer[0] == 7);
            BUSTER_TEST(arguments, module->globals[3].initializer_kind == IR_GLOBAL_INITIALIZER_INTEGER && module->globals[3].initializer_bits == 5);
        }
        BUSTER_TEST(arguments, module->function_count == 14);
        BUSTER_TEST(arguments, module->lowered_function_count == 14);
        u32 field_count = 0;
        u32 index_count = 0;
        u32 pointer_comparison_count = 0;
        for (u32 function_index = 0; function_index < module->function_count; function_index += 1)
        {
            IrFunction* function = &module->functions[function_index];
            for (u32 instruction_index = 0; instruction_index < function->instruction_count; instruction_index += 1)
            {
                field_count += function->instructions[instruction_index].opcode == IR_OPCODE_FIELD;
                index_count += function->instructions[instruction_index].opcode == IR_OPCODE_INDEX;
                pointer_comparison_count += function->instructions[instruction_index].binary_operation == IR_BINARY_POINTER_EQUAL;
            }
        }
        BUSTER_TEST(arguments, field_count == 16);
        BUSTER_TEST(arguments, index_count == 10);
        BUSTER_TEST(arguments, pointer_comparison_count == 1);
        BUSTER_TEST(arguments, ir_validate_canonical_module(aggregate_ir.program, module).error == IR_VALIDATION_NONE);
    }
    scratch_end(aggregate_temporary);
    TemporalArena extension_aggregate_temporary = scratch_begin(0, 0);
    CPreprocessResult extension_aggregate_tokens = c_preprocess(extension_aggregate_temporary.arena,
                                                                S8("typedef long SystemLong;\n"
                                                                   "struct ResourceUsage {\n"
                                                                   "    int prefix;\n"
                                                                   "    __extension__ union {\n"
                                                                   "        long value;\n"
                                                                   "        SystemLong system_value;\n"
                                                                   "    };\n"
                                                                   "    int suffix;\n"
                                                                   "};\n"
                                                                   "long resource_value(long input) {\n"
                                                                   "    struct ResourceUsage usage;\n"
                                                                   "    usage.value = input;\n"
                                                                   "    return usage.system_value;\n"
                                                                   "}\n"),
                                                                (CPreprocessOptions){0});
    CParseResult extension_aggregate_parse = c_parse(extension_aggregate_temporary.arena, extension_aggregate_tokens);
    BUSTER_TEST(arguments, extension_aggregate_parse.diagnostic_count == 0);
    CIRLowerResult extension_aggregate_ir =
        c_lower_to_ir(extension_aggregate_temporary.arena, S8("extension-aggregate.c"), extension_aggregate_tokens, extension_aggregate_parse, lp64_target);
    BUSTER_TEST(arguments, extension_aggregate_ir.diagnostic_count == 0);
    bool found_extension_aggregate_layout = false;
    if (extension_aggregate_ir.program)
    {
        for (u32 type_index = 0; type_index < extension_aggregate_ir.program->types.count; type_index += 1)
        {
            IrType* type = &extension_aggregate_ir.program->types.types[type_index];
            if (type->kind == IR_TYPE_STRUCT && string_equal(type->name, S8("ResourceUsage")))
            {
                found_extension_aggregate_layout = type->layout.resolved && type->layout.size == 24 && type->layout.alignment == 8 && type->field_count == 3;
            }
        }
    }
    BUSTER_TEST(arguments, found_extension_aggregate_layout);
    scratch_end(extension_aggregate_temporary);
    CPreprocessResult local_tokens = c_preprocess(arguments->arena,
                                                  S8("int main(void) {\n"
                                                     "    int value = 2;\n"
                                                     "    value = value * 3;\n"
                                                     "    return value;\n"
                                                     "}\n"),
                                                  (CPreprocessOptions){0});
    CParseResult local_parse = c_parse(arguments->arena, local_tokens);
    BUSTER_TEST(arguments, local_parse.diagnostic_count == 0);
    BUSTER_TEST(arguments, local_parse.declaration_count == 1);
    BUSTER_TEST(arguments, local_parse.entity_count == 2);
    BUSTER_TEST(arguments, local_parse.scope_count == 2);
    BUSTER_TEST(arguments, local_parse.identifier_use_count == 3);
    if (local_parse.entity_count == 2)
    {
        BUSTER_TEST(arguments, local_parse.entities[1].kind == C_ENTITY_LOCAL);
        BUSTER_STRING_TEST(arguments, local_parse.entities[1].name, S8("value"));
        for (u32 use_index = 0; use_index < local_parse.identifier_use_count; use_index += 1)
        {
            BUSTER_TEST(arguments, local_parse.identifier_uses[use_index].entity.value == 1);
        }
    }
    CIRLowerResult local_ir = c_lower_to_ir(arguments->arena, S8("locals.c"), local_tokens, local_parse, target_native);
    BUSTER_TEST(arguments, local_ir.diagnostic_count == 0);
    if (local_ir.program)
    {
        IrFunction* function = local_ir.program->modules[0].functions;
        u32 local_count = 0;
        u32 load_count = 0;
        u32 store_count = 0;
        for (u32 instruction_index = 0; instruction_index < function->instruction_count; instruction_index += 1)
        {
            IrOpcode opcode = function->instructions[instruction_index].opcode;
            local_count += opcode == IR_OPCODE_LOCAL;
            load_count += opcode == IR_OPCODE_LOAD;
            store_count += opcode == IR_OPCODE_STORE;
        }
        BUSTER_TEST(arguments, function->local_count == 1);
        BUSTER_TEST(arguments, local_count == 1);
        BUSTER_TEST(arguments, load_count == 2);
        BUSTER_TEST(arguments, store_count == 2);
        BUSTER_TEST(arguments, ir_validate_canonical_module(local_ir.program, &local_ir.program->modules[0]).error == IR_VALIDATION_NONE);
    }
    CPreprocessResult shadow_tokens = c_preprocess(arguments->arena,
                                                   S8("int main(int value) {\n"
                                                      "    int result = value;\n"
                                                      "    { int value = 2;"
                                                      " result = value; }\n"
                                                      "    return result;\n"
                                                      "}\n"),
                                                   (CPreprocessOptions){0});
    CParseResult shadow_parse = c_parse(arguments->arena, shadow_tokens);
    BUSTER_TEST(arguments, shadow_parse.diagnostic_count == 0);
    BUSTER_TEST(arguments, shadow_parse.scope_count == 3);
    BUSTER_TEST(arguments, shadow_parse.entity_count == 4);
    BUSTER_TEST(arguments, shadow_parse.identifier_use_count == 4);
    if (shadow_parse.identifier_use_count == 4)
    {
        CEntityId parameter = shadow_parse.parameters[0].entity;
        CEntityId outer_result = shadow_parse.identifier_uses[1].entity;
        CEntityId inner_value = shadow_parse.identifier_uses[2].entity;
        BUSTER_TEST(arguments, shadow_parse.identifier_uses[0].entity.value == parameter.value);
        BUSTER_TEST(arguments, inner_value.value != parameter.value);
        BUSTER_TEST(arguments, shadow_parse.identifier_uses[3].entity.value == outer_result.value);
    }
    CIRLowerResult shadow_ir = c_lower_to_ir(arguments->arena, S8("shadow.c"), shadow_tokens, shadow_parse, target_native);
    BUSTER_TEST(arguments, shadow_ir.diagnostic_count == 0);
    if (shadow_ir.program)
    {
        IrFunction* function = shadow_ir.program->modules[0].functions;
        BUSTER_TEST(arguments, function->state == IR_FUNCTION_LOWERED);
        BUSTER_TEST(arguments, function->local_count == 3);
        BUSTER_TEST(arguments, ir_validate_canonical_module(shadow_ir.program, &shadow_ir.program->modules[0]).error == IR_VALIDATION_NONE);
    }
    TemporalArena control_flow_temporary = scratch_begin(0, 0);
    CPreprocessResult if_tokens = c_preprocess(control_flow_temporary.arena,
                                               S8("int choose(int value) {\n"
                                                  "    int result = 1;\n"
                                                  "    if (value) {\n"
                                                  "        result = 2;\n"
                                                  "    } else {\n"
                                                  "        result = 3;\n"
                                                  "    }\n"
                                                  "    return result;\n"
                                                  "}\n"
                                                  "int select_return(int value) {\n"
                                                  "    switch (value) {\n"
                                                  "    case 1: return 10;\n"
                                                  "    default: return 20;\n"
                                                  "    }\n"
                                                  "}\n"),
                                               (CPreprocessOptions){0});
    CParseResult if_parse = c_parse(control_flow_temporary.arena, if_tokens);
    BUSTER_TEST(arguments, if_parse.diagnostic_count == 0);
    CIRLowerResult if_ir = c_lower_to_ir(control_flow_temporary.arena, S8("if.c"), if_tokens, if_parse, target_native);
    BUSTER_TEST(arguments, if_ir.diagnostic_count == 0);
    if (if_ir.program)
    {
        IrModule* module = &if_ir.program->modules[0];
        IrFunction* function = module->functions;
        u32 branch_count = 0;
        u32 branch_if_count = 0;
        u32 return_count = 0;
        for (u32 instruction_index = 0; instruction_index < function->instruction_count; instruction_index += 1)
        {
            IrOpcode opcode = function->instructions[instruction_index].opcode;
            branch_count += opcode == IR_OPCODE_BRANCH;
            branch_if_count += opcode == IR_OPCODE_BRANCH_IF;
            return_count += opcode == IR_OPCODE_RETURN;
        }
        BUSTER_TEST(arguments, function->block_count == 4);
        BUSTER_TEST(arguments, branch_if_count == 1);
        BUSTER_TEST(arguments, branch_count == 2);
        BUSTER_TEST(arguments, return_count == 1);
        BUSTER_TEST(arguments, ir_validate_canonical_module(if_ir.program, module).error == IR_VALIDATION_NONE);
    }
    CPreprocessResult else_if_tokens = c_preprocess(control_flow_temporary.arena,
                                                    S8("int classify(int value) {\n"
                                                       "    int result = 0;\n"
                                                       "    if (value < 0) {\n"
                                                       "        result = -1;\n"
                                                       "    } else if (value > 0) {\n"
                                                       "        result = 1;\n"
                                                       "    } else {\n"
                                                       "        result = 2;\n"
                                                       "    }\n"
                                                       "    return result;\n"
                                                       "}\n"),
                                                    (CPreprocessOptions){0});
    CParseResult else_if_parse = c_parse(control_flow_temporary.arena, else_if_tokens);
    BUSTER_TEST(arguments, else_if_parse.diagnostic_count == 0);
    CIRLowerResult else_if_ir = c_lower_to_ir(control_flow_temporary.arena, S8("else_if.c"), else_if_tokens, else_if_parse, target_native);
    BUSTER_TEST(arguments, else_if_ir.diagnostic_count == 0);
    if (else_if_ir.program)
    {
        IrModule* module = &else_if_ir.program->modules[0];
        IrFunction* function = module->functions;
        u32 branch_if_count = 0;
        for (u32 instruction_index = 0; instruction_index < function->instruction_count; instruction_index += 1)
        {
            branch_if_count += function->instructions[instruction_index].opcode == IR_OPCODE_BRANCH_IF;
        }
        BUSTER_TEST(arguments, function->block_count == 7);
        BUSTER_TEST(arguments, branch_if_count == 2);
        BUSTER_TEST(arguments, ir_validate_canonical_module(else_if_ir.program, module).error == IR_VALIDATION_NONE);
    }
    CPreprocessResult while_tokens = c_preprocess(control_flow_temporary.arena,
                                                  S8("int count_down(int value) {\n"
                                                     "    while (value) {\n"
                                                     "        if (value == 2) {\n"
                                                     "            break;\n"
                                                     "        }\n"
                                                     "        value = value - 1;\n"
                                                     "        continue;\n"
                                                     "    }\n"
                                                     "    return value;\n"
                                                     "}\n"),
                                                  (CPreprocessOptions){0});
    CParseResult while_parse = c_parse(control_flow_temporary.arena, while_tokens);
    BUSTER_TEST(arguments, while_parse.diagnostic_count == 0);
    CIRLowerResult while_ir = c_lower_to_ir(control_flow_temporary.arena, S8("while.c"), while_tokens, while_parse, target_native);
    BUSTER_TEST(arguments, while_ir.diagnostic_count == 0);
    if (while_ir.program)
    {
        IrModule* module = &while_ir.program->modules[0];
        IrFunction* function = module->functions;
        u32 branch_count = 0;
        u32 branch_if_count = 0;
        for (u32 instruction_index = 0; instruction_index < function->instruction_count; instruction_index += 1)
        {
            IrOpcode opcode = function->instructions[instruction_index].opcode;
            branch_count += opcode == IR_OPCODE_BRANCH;
            branch_if_count += opcode == IR_OPCODE_BRANCH_IF;
        }
        BUSTER_TEST(arguments, function->block_count == 6);
        BUSTER_TEST(arguments, branch_if_count == 2);
        BUSTER_TEST(arguments, branch_count == 3);
        BUSTER_TEST(arguments, ir_validate_canonical_module(while_ir.program, module).error == IR_VALIDATION_NONE);
    }
    CPreprocessResult for_tokens = c_preprocess(control_flow_temporary.arena,
                                                S8("int sum_to(int count) {\n"
                                                   "    int sum = 0;\n"
                                                   "    for (int index = 0; index < count;"
                                                   " index++) {\n"
                                                   "        sum += index;\n"
                                                   "    }\n"
                                                   "    return sum;\n"
                                                   "}\n"),
                                                (CPreprocessOptions){0});
    CParseResult for_parse = c_parse(control_flow_temporary.arena, for_tokens);
    BUSTER_TEST(arguments, for_parse.diagnostic_count == 0);
    CIRLowerResult for_ir = c_lower_to_ir(control_flow_temporary.arena, S8("for.c"), for_tokens, for_parse, target_native);
    BUSTER_TEST(arguments, for_ir.diagnostic_count == 0);
    if (for_ir.program)
    {
        IrModule* module = &for_ir.program->modules[0];
        IrFunction* function = module->functions;
        u32 branch_count = 0;
        u32 branch_if_count = 0;
        for (u32 instruction_index = 0; instruction_index < function->instruction_count; instruction_index += 1)
        {
            IrOpcode opcode = function->instructions[instruction_index].opcode;
            branch_count += opcode == IR_OPCODE_BRANCH;
            branch_if_count += opcode == IR_OPCODE_BRANCH_IF;
        }
        BUSTER_TEST(arguments, function->block_count == 5);
        BUSTER_TEST(arguments, branch_if_count == 1);
        BUSTER_TEST(arguments, branch_count == 3);
        BUSTER_TEST(arguments, ir_validate_canonical_module(for_ir.program, module).error == IR_VALIDATION_NONE);
    }
    CPreprocessResult for_preheader_tokens = c_preprocess(control_flow_temporary.arena,
                                                          S8("int sum_after_selection(int count) {\n"
                                                             "    int sum = 0;\n"
                                                             "    sum = count ? 1 : 2;\n"
                                                             "    for (int index = 0; index < count;"
                                                             " index += 1) {\n"
                                                             "        sum += index;\n"
                                                             "    }\n"
                                                             "    return sum;\n"
                                                             "}\n"),
                                                          (CPreprocessOptions){0});
    CParseResult for_preheader_parse = c_parse(control_flow_temporary.arena, for_preheader_tokens);
    BUSTER_TEST(arguments, for_preheader_parse.diagnostic_count == 0);
    CIRLowerResult for_preheader_ir =
        c_lower_to_ir(control_flow_temporary.arena, S8("for-preheader.c"), for_preheader_tokens, for_preheader_parse, target_native);
    BUSTER_TEST(arguments, for_preheader_ir.diagnostic_count == 0);
    if (for_preheader_ir.program)
    {
        IrModule* module = &for_preheader_ir.program->modules[0];
        BUSTER_TEST(arguments, module->function_count == 1);
        BUSTER_TEST(arguments, module->functions[0].block_count >= 7);
        BUSTER_TEST(arguments, ir_validate_canonical_module(for_preheader_ir.program, module).error == IR_VALIDATION_NONE);
    }
    CPreprocessResult single_body_tokens = c_preprocess(control_flow_temporary.arena,
                                                        S8("int single_bodies(int value) {\n"
                                                           "    int index = 0;\n"
                                                           "    if (value) value = 3;"
                                                           " else value = 2;\n"
                                                           "    while (value > 1)"
                                                           " value = value - 1;\n"
                                                           "    do value = value + 1;"
                                                           " while (value < 2);\n"
                                                           "    for (index = 0; index < 2;"
                                                           " index = index + 1)"
                                                           " value = value + index;\n"
                                                           "    return value;\n"
                                                           "}\n"),
                                                        (CPreprocessOptions){0});
    CParseResult single_body_parse = c_parse(control_flow_temporary.arena, single_body_tokens);
    BUSTER_TEST(arguments, single_body_parse.diagnostic_count == 0);
    CIRLowerResult single_body_ir = c_lower_to_ir(control_flow_temporary.arena, S8("single_bodies.c"), single_body_tokens, single_body_parse, target_native);
    BUSTER_TEST(arguments, single_body_ir.diagnostic_count == 0);
    if (single_body_ir.program)
    {
        BUSTER_TEST(arguments, ir_validate_canonical_module(single_body_ir.program, &single_body_ir.program->modules[0]).error == IR_VALIDATION_NONE);
    }
    CPreprocessResult switch_tokens = c_preprocess(control_flow_temporary.arena,
                                                   S8("int select_value(int value) {\n"
                                                      "    int result = 0;\n"
                                                      "    switch (value) {\n"
                                                      "    case 1:\n"
                                                      "        result = 10;\n"
                                                      "        break;\n"
                                                      "    case 2:\n"
                                                      "        result = 20;\n"
                                                      "    default:\n"
                                                      "        result += 1;\n"
                                                      "        break;\n"
                                                      "    }\n"
                                                      "    return result;\n"
                                                      "}\n"),
                                                   (CPreprocessOptions){0});
    CParseResult switch_parse = c_parse(control_flow_temporary.arena, switch_tokens);
    BUSTER_TEST(arguments, switch_parse.diagnostic_count == 0);
    CIRLowerResult switch_ir = c_lower_to_ir(control_flow_temporary.arena, S8("switch.c"), switch_tokens, switch_parse, target_native);
    BUSTER_TEST(arguments, switch_ir.diagnostic_count == 0);
    if (switch_ir.program)
    {
        IrModule* module = &switch_ir.program->modules[0];
        IrFunction* function = module->functions;
        u32 switch_count = 0;
        for (u32 instruction_index = 0; instruction_index < function->instruction_count; instruction_index += 1)
        {
            switch_count += function->instructions[instruction_index].opcode == IR_OPCODE_SWITCH;
        }
        BUSTER_TEST(arguments, function->block_count == 5);
        BUSTER_TEST(arguments, switch_count == 1);
        BUSTER_TEST(arguments, ir_validate_canonical_module(switch_ir.program, module).error == IR_VALIDATION_NONE);
    }
    CPreprocessResult goto_tokens = c_preprocess(control_flow_temporary.arena,
                                                 S8("int jump(int value) {\n"
                                                    "    goto target;\n"
                                                    "    value = 99;\n"
                                                    "target:\n"
                                                    "    value += 1;\n"
                                                    "    if (value > 2) {\n"
                                                    "        goto done;\n"
                                                    "    }\n"
                                                    "    value += 2;\n"
                                                    "done:\n"
                                                    "    return value;\n"
                                                    "}\n"),
                                                 (CPreprocessOptions){0});
    CParseResult goto_parse = c_parse(control_flow_temporary.arena, goto_tokens);
    BUSTER_TEST(arguments, goto_parse.diagnostic_count == 0);
    CIRLowerResult goto_ir = c_lower_to_ir(control_flow_temporary.arena, S8("goto.c"), goto_tokens, goto_parse, target_native);
    BUSTER_TEST(arguments, goto_ir.diagnostic_count == 0);
    if (goto_ir.program)
    {
        IrModule* module = &goto_ir.program->modules[0];
        BUSTER_TEST(arguments, module->lowered_function_count == 1);
        BUSTER_TEST(arguments, module->functions[0].block_count >= 3);
        BUSTER_TEST(arguments, ir_validate_canonical_module(goto_ir.program, module).error == IR_VALIDATION_NONE);
    }
    CPreprocessResult short_circuit_tokens = c_preprocess(control_flow_temporary.arena,
                                                          S8("int observe(int value) {\n"
                                                             "    return value;\n"
                                                             "}\n"
                                                             "int short_circuit(int left, int right) {\n"
                                                             "    if (left && observe(right)"
                                                             " || observe(left)) {\n"
                                                             "        return 1;\n"
                                                             "    }\n"
                                                             "    return 0;\n"
                                                             "}\n"
                                                             "int logical_value(int left, int right) {\n"
                                                             "    int result = left && observe(right);\n"
                                                             "    return result || observe(left);\n"
                                                             "}\n"
                                                             "int conditional_value(int condition,"
                                                             " int left, int right) {\n"
                                                             "    int selected = condition ? left : right;\n"
                                                             "    return condition && left"
                                                             " ? observe(selected) : selected + 1;\n"
                                                             "}\n"
                                                             "int nested_conditional(int first,"
                                                             " int second, int third) {\n"
                                                             "    return first ?"
                                                             " (second ? 1 : 2) :"
                                                             " (third ? 3 : 4);\n"
                                                             "}\n"
                                                             "int embedded_logical(int left,"
                                                             " int right) {\n"
                                                             "    return"
                                                             " (left && observe(right)) == 1;\n"
                                                             "}\n"
                                                             "int embedded_conditional(int condition,"
                                                             " int value) {\n"
                                                             "    return"
                                                             " (condition ? observe(value) : 0) + 2;\n"
                                                             "}\n"),
                                                          (CPreprocessOptions){0});
    CParseResult short_circuit_parse = c_parse(control_flow_temporary.arena, short_circuit_tokens);
    BUSTER_TEST(arguments, short_circuit_parse.diagnostic_count == 0);
    CIRLowerResult short_circuit_ir =
        c_lower_to_ir(control_flow_temporary.arena, S8("short_circuit.c"), short_circuit_tokens, short_circuit_parse, target_native);
    BUSTER_TEST(arguments, short_circuit_ir.diagnostic_count == 0);
    if (short_circuit_ir.program)
    {
        IrModule* module = &short_circuit_ir.program->modules[0];
        IrFunction* function = &module->functions[1];
        u32 call_count = 0;
        u32 branch_if_count = 0;
        for (u32 instruction_index = 0; instruction_index < function->instruction_count; instruction_index += 1)
        {
            IrOpcode opcode = function->instructions[instruction_index].opcode;
            call_count += opcode == IR_OPCODE_CALL;
            branch_if_count += opcode == IR_OPCODE_BRANCH_IF;
        }
        BUSTER_TEST(arguments, call_count == 2);
        BUSTER_TEST(arguments, branch_if_count == 3);
        IrFunction* value_function = &module->functions[2];
        u32 value_branch_if_count = 0;
        for (u32 instruction_index = 0; instruction_index < value_function->instruction_count; instruction_index += 1)
        {
            value_branch_if_count += value_function->instructions[instruction_index].opcode == IR_OPCODE_BRANCH_IF;
        }
        BUSTER_TEST(arguments, value_branch_if_count == 4);
        IrFunction* conditional_function = &module->functions[3];
        u32 conditional_branch_if_count = 0;
        for (u32 instruction_index = 0; instruction_index < conditional_function->instruction_count; instruction_index += 1)
        {
            conditional_branch_if_count += conditional_function->instructions[instruction_index].opcode == IR_OPCODE_BRANCH_IF;
        }
        BUSTER_TEST(arguments, conditional_branch_if_count == 3);
        BUSTER_TEST(arguments, conditional_function->local_count == 6);
        IrFunction* nested_function = &module->functions[4];
        u32 nested_branch_if_count = 0;
        for (u32 instruction_index = 0; instruction_index < nested_function->instruction_count; instruction_index += 1)
        {
            nested_branch_if_count += nested_function->instructions[instruction_index].opcode == IR_OPCODE_BRANCH_IF;
        }
        BUSTER_TEST(arguments, nested_branch_if_count == 3);
        for (u32 function_index = 5; function_index <= 6; function_index += 1)
        {
            IrFunction* embedded = &module->functions[function_index];
            u32 embedded_call_count = 0;
            u32 embedded_branch_if_count = 0;
            for (u32 instruction_index = 0; instruction_index < embedded->instruction_count; instruction_index += 1)
            {
                IrOpcode opcode = embedded->instructions[instruction_index].opcode;
                embedded_call_count += opcode == IR_OPCODE_CALL;
                embedded_branch_if_count += opcode == IR_OPCODE_BRANCH_IF;
            }
            BUSTER_TEST(arguments, embedded_call_count == 1);
            BUSTER_TEST(arguments, embedded_branch_if_count >= 1);
        }
        IrValidationResult short_validation = ir_validate_canonical_module(short_circuit_ir.program, module);
        BUSTER_TEST(arguments, short_validation.error == IR_VALIDATION_NONE);
    }
    scratch_end(control_flow_temporary);
    CPreprocessResult undeclared_tokens = c_preprocess(arguments->arena,
                                                       S8("int main(void)"
                                                          " { return missing; }\n"),
                                                       (CPreprocessOptions){0});
    CParseResult undeclared_parse = c_parse(arguments->arena, undeclared_tokens);
    BUSTER_TEST(arguments, undeclared_parse.diagnostic_count == 1);
    BUSTER_TEST(arguments, undeclared_parse.diagnostic_count == 1 && undeclared_parse.diagnostics[0].kind == C_DIAGNOSTIC_UNDECLARED_IDENTIFIER);
    CPreprocessResult argument_tokens = c_preprocess(arguments->arena,
                                                     S8("static int identity(int value)\n"
                                                        "{\n"
                                                        "    return value;\n"
                                                        "}\n"
                                                        "int main(void)\n"
                                                        "{\n"
                                                        "    return "
                                                        "(identity(1 + 2) == 3) - 1;\n"
                                                        "}\n"),
                                                     (CPreprocessOptions){0});
    CParseResult argument_parse = c_parse(arguments->arena, argument_tokens);
    CIRLowerResult argument_ir = c_lower_to_ir(arguments->arena, S8("arguments.c"), argument_tokens, argument_parse, target_native);
    BUSTER_TEST(arguments, argument_ir.diagnostic_count == 0);
    if (argument_ir.program)
    {
        IrModule* module = &argument_ir.program->modules[0];
        BUSTER_TEST(arguments, module->function_count == 2);
        IrFunction* identity = module->functions;
        IrType* identity_type = ir_type_from_id(&argument_ir.program->types, identity->canonical_type);
        BUSTER_TEST(arguments, identity_type->parameter_count == 1);
        BUSTER_TEST(arguments, identity->instructions[1].opcode == IR_OPCODE_ARGUMENT);
        IrFunction* main_function = module->functions + 1;
        u32 call_count = 0;
        u32 comparison_count = 0;
        u32 cast_count = 0;
        for (u32 instruction_index = 0; instruction_index < main_function->instruction_count; instruction_index += 1)
        {
            IrInstruction* instruction = main_function->instructions + instruction_index;
            if (instruction->opcode == IR_OPCODE_CALL)
            {
                call_count += 1;
                BUSTER_TEST(arguments, instruction->operand_count == 2);
                IrInstruction* argument = main_function->instructions + main_function->values[instruction->operands[1].value].definition.value;
                BUSTER_TEST(arguments, argument->opcode == IR_OPCODE_BINARY);
                BUSTER_TEST(arguments, argument->binary_operation == IR_BINARY_INTEGER_ADD);
            }
            comparison_count += instruction->opcode == IR_OPCODE_BINARY && instruction->binary_operation == IR_BINARY_INTEGER_EQUAL;
            cast_count += instruction->opcode == IR_OPCODE_CAST && instruction->conversion_operation == IR_CONVERSION_INTEGER_ZERO_EXTEND;
        }
        BUSTER_TEST(arguments, call_count == 1);
        BUSTER_TEST(arguments, comparison_count == 1);
        BUSTER_TEST(arguments, cast_count == 1);
        BUSTER_TEST(arguments, ir_validate_canonical_module(argument_ir.program, module).error == IR_VALIDATION_NONE);
    }
    TemporalArena vector_temporary = scratch_begin(0, 0);
    CPreprocessResult vector_tokens = c_preprocess(vector_temporary.arena,
                                                   S8("typedef float Float4 "
                                                      "__attribute__((vector_size(16)));"
                                                      " typedef int Int4 "
                                                      "__attribute__((vector_size(16)));"
                                                      " typedef struct VectorPair"
                                                      " { Float4 left; Float4 right; }"
                                                      " VectorPair;"
                                                      " VectorPair identity"
                                                      "(VectorPair value)"
                                                      " { return value; }"
                                                      " Float4 arithmetic"
                                                      "(Float4 left, Float4 right)"
                                                      " { return -(left + right * 2.0f); }"
                                                      " Int4 compare(Int4 left, Int4 right)"
                                                      " { return left > right; }"
                                                      " int main(void)"
                                                      " { VectorPair value = { 0 };"
                                                      " return 0; }\n"),
                                                   (CPreprocessOptions){0});
    CParseResult vector_parse = c_parse(vector_temporary.arena, vector_tokens);
    BUSTER_TEST(arguments, vector_parse.diagnostic_count == 0);
    bool found_c_vector = false;
    for (u32 type_index = 0; type_index < vector_parse.type_count; type_index += 1)
    {
        CType* type = vector_parse.types + type_index;
        found_c_vector |= type->kind == C_TYPE_VECTOR && type->vector_byte_size == 16;
    }
    BUSTER_TEST(arguments, found_c_vector);
    CIRLowerResult vector_ir = c_lower_to_ir(vector_temporary.arena, S8("vector.c"), vector_tokens, vector_parse, target_native);
    BUSTER_TEST(arguments, vector_ir.diagnostic_count == 0);
    if (vector_ir.program)
    {
        bool found_ir_vector = false;
        bool found_vector_pair = false;
        bool found_vector_mask = false;
        for (u32 type_index = 0; type_index < vector_ir.program->types.count; type_index += 1)
        {
            IrType* type = vector_ir.program->types.types + type_index;
            found_ir_vector |= type->kind == IR_TYPE_VECTOR && type->layout.size == 16 && type->element_count == 4;
            found_vector_pair |= type->kind == IR_TYPE_STRUCT && type->field_count == 2 && type->layout.size == 32;
            if (type->kind == IR_TYPE_VECTOR && type->element_count == 4)
            {
                IrType* element = ir_type_from_id(&vector_ir.program->types, type->element_type);
                found_vector_mask |= element && element->kind == IR_TYPE_INTEGER && element->is_signed && element->bit_width == 32;
            }
        }
        BUSTER_TEST(arguments, found_ir_vector);
        BUSTER_TEST(arguments, found_vector_pair);
        BUSTER_TEST(arguments, found_vector_mask);
        IrModule* module = vector_ir.program->modules;
        BUSTER_TEST(arguments, module->function_count == 4);
        u32 vector_binary_count = 0;
        u32 vector_unary_count = 0;
        u32 vector_comparison_count = 0;
        for (u32 function_index = 0; function_index < module->function_count; function_index += 1)
        {
            IrFunction* function = module->functions + function_index;
            for (u32 instruction_index = 0; instruction_index < function->instruction_count; instruction_index += 1)
            {
                IrInstruction* instruction = function->instructions + instruction_index;
                vector_unary_count += instruction->opcode == IR_OPCODE_UNARY && instruction->unary_operation >= IR_UNARY_VECTOR_INTEGER_NEGATE &&
                                      instruction->unary_operation <= IR_UNARY_VECTOR_INTEGER_BITWISE_NOT;
                vector_binary_count += instruction->opcode == IR_OPCODE_BINARY && instruction->binary_operation >= IR_BINARY_VECTOR_INTEGER_ADD &&
                                       instruction->binary_operation <= IR_BINARY_VECTOR_FLOAT_GREATER_EQUAL;
                vector_comparison_count += instruction->opcode == IR_OPCODE_BINARY && instruction->binary_operation >= IR_BINARY_VECTOR_INTEGER_EQUAL &&
                                           instruction->binary_operation <= IR_BINARY_VECTOR_FLOAT_GREATER_EQUAL;
            }
        }
        BUSTER_TEST(arguments, vector_unary_count == 1);
        BUSTER_TEST(arguments, vector_binary_count == 3);
        BUSTER_TEST(arguments, vector_comparison_count == 1);
        BUSTER_TEST(arguments, ir_validate_canonical_module(vector_ir.program, module).error == IR_VALIDATION_NONE);
    }
    scratch_end(vector_temporary);
    TemporalArena variadic_call_temporary = scratch_begin(0, 0);
    CPreprocessResult variadic_call_tokens = c_preprocess(variadic_call_temporary.arena,
                                                          S8("int sink(int fixed, ...);"
                                                             " int main(void)"
                                                             " { return sink(0,"
                                                             " (char)1, (float)2); }"),
                                                          (CPreprocessOptions){0});
    CParseResult variadic_call_parse = c_parse(variadic_call_temporary.arena, variadic_call_tokens);
    CIRLowerResult variadic_call_ir =
        c_lower_to_ir(variadic_call_temporary.arena, S8("variadic-call.c"), variadic_call_tokens, variadic_call_parse, target_native);
    BUSTER_TEST(arguments, variadic_call_parse.diagnostic_count == 0);
    BUSTER_TEST(arguments, variadic_call_ir.diagnostic_count == 0);
    if (variadic_call_ir.program)
    {
        IrModule* variadic_call_module = &variadic_call_ir.program->modules[0];
        IrInstruction* call = 0;
        IrFunction* main_function = 0;
        for (u32 function_index = 0; function_index < variadic_call_module->function_count; function_index += 1)
        {
            IrFunction* function = variadic_call_module->functions + function_index;
            if (string_equal(function->name, S8("main")))
            {
                main_function = function;
                for (u32 instruction_index = 0; instruction_index < function->instruction_count; instruction_index += 1)
                {
                    if (function->instructions[instruction_index].opcode == IR_OPCODE_CALL)
                    {
                        call = function->instructions + instruction_index;
                        break;
                    }
                }
                break;
            }
        }
        BUSTER_TEST(arguments, main_function != 0);
        BUSTER_TEST(arguments, call != 0);
        if (main_function && call)
        {
            BUSTER_TEST(arguments, call->operand_count == 4);
            IrType* promoted_character = ir_type_from_id(&variadic_call_ir.program->types, main_function->values[call->operands[2].value].canonical_type);
            IrType* promoted_float = ir_type_from_id(&variadic_call_ir.program->types, main_function->values[call->operands[3].value].canonical_type);
            BUSTER_TEST(arguments, promoted_character && promoted_character->kind == IR_TYPE_INTEGER && promoted_character->bit_width == 32);
            BUSTER_TEST(arguments, promoted_float && promoted_float->kind == IR_TYPE_FLOAT && promoted_float->bit_width == 64);
        }
        BUSTER_TEST(arguments, ir_validate_canonical_module(variadic_call_ir.program, variadic_call_module).error == IR_VALIDATION_NONE);
    }
    scratch_end(variadic_call_temporary);
    TemporalArena c23_va_start_temporary = scratch_begin(0, 0);
    CPreprocessResult c23_va_start_tokens = c_preprocess(c23_va_start_temporary.arena,
                                                         S8("typedef void *va_list;"
                                                            " int first(int count, ...)"
                                                            " { va_list arguments;"
                                                            " __builtin_c23_va_start("
                                                            "arguments, count);"
                                                            " int value = __builtin_va_arg("
                                                            "arguments, int);"
                                                            " __builtin_va_end(arguments);"
                                                            " return value; }"),
                                                         (CPreprocessOptions){
                                                             .dialect = C_PREPROCESS_DIALECT_C23,
                                                         });
    CParseResult c23_va_start_parse = c_parse(c23_va_start_temporary.arena, c23_va_start_tokens);
    CIRLowerResult c23_va_start_ir = c_lower_to_ir(c23_va_start_temporary.arena, S8("c23-va-start.c"), c23_va_start_tokens, c23_va_start_parse, target_native);
    BUSTER_TEST(arguments, c23_va_start_tokens.diagnostic_count == 0);
    BUSTER_TEST(arguments, c23_va_start_parse.diagnostic_count == 0);
    BUSTER_TEST(arguments, c23_va_start_ir.diagnostic_count == 0);
    if (c23_va_start_ir.program)
    {
        IrModule* module = c23_va_start_ir.program->modules;
        u32 va_start_count = 0;
        for (u32 function_index = 0; function_index < module->function_count; function_index += 1)
        {
            IrFunction* function = module->functions + function_index;
            for (u32 instruction_index = 0; instruction_index < function->instruction_count; instruction_index += 1)
            {
                va_start_count += function->instructions[instruction_index].opcode == IR_OPCODE_VA_START;
            }
        }
        BUSTER_TEST(arguments, va_start_count == 1);
        BUSTER_TEST(arguments, ir_validate_canonical_module(c23_va_start_ir.program, module).error == IR_VALIDATION_NONE);
    }
    scratch_end(c23_va_start_temporary);
    TemporalArena debug_trap_temporary = scratch_begin(0, 0);
    CPreprocessResult debug_trap_tokens = c_preprocess(debug_trap_temporary.arena,
                                                       S8("int main(void)"
                                                          " { __builtin_debugtrap();"
                                                          " return 0; }"),
                                                       (CPreprocessOptions){0});
    CParseResult debug_trap_parse = c_parse(debug_trap_temporary.arena, debug_trap_tokens);
    CIRLowerResult debug_trap_ir = c_lower_to_ir(debug_trap_temporary.arena, S8("debug-trap.c"), debug_trap_tokens, debug_trap_parse, target_native);
    BUSTER_TEST(arguments, debug_trap_parse.diagnostic_count == 0);
    BUSTER_TEST(arguments, debug_trap_ir.diagnostic_count == 0);
    if (debug_trap_ir.program)
    {
        IrModule* debug_trap_module = &debug_trap_ir.program->modules[0];
        u32 debug_trap_count = 0;
        for (u32 instruction_index = 0; instruction_index < debug_trap_module->functions[0].instruction_count; instruction_index += 1)
        {
            debug_trap_count += debug_trap_module->functions[0].instructions[instruction_index].opcode == IR_OPCODE_DEBUG_TRAP;
        }
        BUSTER_TEST(arguments, debug_trap_count == 1);
        BUSTER_TEST(arguments, ir_validate_canonical_module(debug_trap_ir.program, debug_trap_module).error == IR_VALIDATION_NONE);
    }
    scratch_end(debug_trap_temporary);
    TemporalArena compound_temporary = scratch_begin(0, 0);
    CPreprocessResult compound_tokens = c_preprocess(compound_temporary.arena,
                                                     S8("struct Pair"
                                                        " { int left; int right; };"
                                                        " int main(void)"
                                                        " { return ((struct Pair)"
                                                        " { .right = 7, .left = 3 }).right; }"),
                                                     (CPreprocessOptions){0});
    CParseResult compound_parse = c_parse(compound_temporary.arena, compound_tokens);
    CIRLowerResult compound_ir = c_lower_to_ir(compound_temporary.arena, S8("compound-literal.c"), compound_tokens, compound_parse, target_native);
    BUSTER_TEST(arguments, compound_parse.diagnostic_count == 0);
    BUSTER_TEST(arguments, compound_ir.diagnostic_count == 0);
    if (compound_ir.program)
    {
        IrModule* compound_module = &compound_ir.program->modules[0];
        u32 aggregate_count = 0;
        u32 field_count = 0;
        for (u32 instruction_index = 0; instruction_index < compound_module->functions[0].instruction_count; instruction_index += 1)
        {
            IrOpcode opcode = compound_module->functions[0].instructions[instruction_index].opcode;
            aggregate_count += opcode == IR_OPCODE_AGGREGATE;
            field_count += opcode == IR_OPCODE_FIELD;
        }
        BUSTER_TEST(arguments, aggregate_count == 1);
        BUSTER_TEST(arguments, field_count == 1);
        BUSTER_TEST(arguments, ir_validate_canonical_module(compound_ir.program, compound_module).error == IR_VALIDATION_NONE);
    }
    scratch_end(compound_temporary);
    TemporalArena regression_temporary = scratch_begin(0, 0);
    CPreprocessResult regression_tokens = c_preprocess(regression_temporary.arena,
                                                       S8("typedef unsigned char char8;"
                                                          " typedef struct Id Id;"
                                                          " struct Id { unsigned value; };"
                                                          " struct Item"
                                                          " { Id id; int values[2];"
                                                          " int thread_local; int enabled; };"
                                                          " static struct Item *pick("
                                                          " struct Item *item) { return item; }"
                                                          " int main(void) {"
                                                          " enum { PICK = 1 };"
                                                          " typedef unsigned int LocalIndex;"
                                                          " typedef struct LocalPair"
                                                          " { int value; } LocalPair;"
                                                          " static char8 const text[] = \"x\";"
                                                          " LocalIndex condition = 1;"
                                                          " LocalPair local_pair = { .value = 2 };"
                                                          " struct Item item = (struct Item){"
                                                          " .id = (Id){ .value = 0u },"
                                                          " .thread_local = 5,"
                                                          " .enabled = condition != 0 ||"
                                                          " condition == 0 };"
                                                          " item.values[0] = 3;"
                                                          " item.values[1] = 4;"
                                                          " int alternate[2] = { 9, 10 };"
                                                          " int *selected_values = condition ?"
                                                          " item.values : alternate;"
                                                          " int pointer_order = selected_values <"
                                                          " selected_values + 1;"
                                                          " double converted = condition ?"
                                                          " (_Bool)1 : 2.0;"
                                                          " int nested = condition ?"
                                                          " (condition ? 6 : 7) : 8;"
                                                          " int selected = condition &&"
                                                          " (condition ?"
                                                          " pick(&item)->thread_local :"
                                                          " pick(&item)->values[0]);"
                                                          " int conditional_truth = 0;"
                                                          " if (condition ? selected : nested)"
                                                          " { conditional_truth = 1; }"
                                                          " switch ('a')"
                                                          " { case 'a': conditional_truth += 1;"
                                                          " break; default: break; }"
                                                          " return pick(&item)->values["
                                                          " condition ? 0u : 1u]"
                                                          " + selected_values[0]"
                                                          " + item.thread_local + text[0]"
                                                          " + nested + selected"
                                                          " + conditional_truth"
                                                          " + pointer_order"
                                                          " + local_pair.value"
                                                          " + (int)converted + PICK;"
                                                          " }"),
                                                       (CPreprocessOptions){0});
    CParseResult regression_parse = c_parse(regression_temporary.arena, regression_tokens);
    CIRLowerResult regression_ir = c_lower_to_ir(regression_temporary.arena, S8("c-regressions.c"), regression_tokens, regression_parse, target_native);
    BUSTER_TEST(arguments, regression_tokens.diagnostic_count == 0);
    BUSTER_TEST(arguments, regression_parse.diagnostic_count == 0);
    BUSTER_TEST(arguments, regression_ir.diagnostic_count == 0);
    if (regression_ir.program)
    {
        IrModule* regression_module = &regression_ir.program->modules[0];
        BUSTER_TEST(arguments, regression_module->function_count >= 2);
        BUSTER_TEST(arguments, regression_module->global_count >= 1);
        BUSTER_TEST(arguments, ir_validate_canonical_module(regression_ir.program, regression_module).error == IR_VALIDATION_NONE);
    }
    scratch_end(regression_temporary);
    Arena* scratch_lifetime_arena = arena_create((ArenaCreation){
        .reserved_size = BUSTER_MB(256),
    });
    enum
    {
        C_IR_SCRATCH_STRESS_TYPE_COUNT = 4096,
        C_IR_SCRATCH_STRESS_EXPRESSION_COUNT = 4096,
    };
    u32 scratch_fragment_capacity = C_IR_SCRATCH_STRESS_TYPE_COUNT + C_IR_SCRATCH_STRESS_EXPRESSION_COUNT + 2;
    String8* scratch_fragments = arena_allocate(scratch_lifetime_arena, String8, scratch_fragment_capacity);
    u32 scratch_fragment_count = 0;
    for (u32 index = 0; index < C_IR_SCRATCH_STRESS_TYPE_COUNT; index += 1)
    {
        scratch_fragments[scratch_fragment_count++] = string_format(scratch_lifetime_arena,
                                                                    S8("struct Padding{u32}"
                                                                       " {{ int value; }};"),
                                                                    index);
    }
    scratch_fragments[scratch_fragment_count++] = S8("struct ScratchItem { int value; };"
                                                     " static struct ScratchItem *"
                                                     "scratch_identity("
                                                     "struct ScratchItem *item)"
                                                     " { return item; }"
                                                     " int scratch_stress(void)"
                                                     " { struct ScratchItem item = { 0 };");
    for (u32 index = 0; index < C_IR_SCRATCH_STRESS_EXPRESSION_COUNT; index += 1)
    {
        scratch_fragments[scratch_fragment_count++] = string_format(scratch_lifetime_arena,
                                                                    S8("item.value +="
                                                                       " scratch_identity(&item)"
                                                                       "->value +"
                                                                       " (int)sizeof("
                                                                       "struct Padding{u32});"),
                                                                    index);
    }
    scratch_fragments[scratch_fragment_count++] = S8("return item.value; }");
    BUSTER_TEST(arguments, scratch_fragment_count == scratch_fragment_capacity);
    String8 scratch_lifetime_source = string_join_arena(scratch_lifetime_arena,
                                                        (SliceString8){
                                                            .pointer = scratch_fragments,
                                                            .length = scratch_fragment_count,
                                                        },
                                                        false);
    CPreprocessResult scratch_lifetime_tokens = c_preprocess(scratch_lifetime_arena, scratch_lifetime_source, (CPreprocessOptions){0});
    CParseResult scratch_lifetime_parse = c_parse(scratch_lifetime_arena, scratch_lifetime_tokens);
    CIRLowerResult scratch_lifetime_ir =
        c_lower_to_ir(scratch_lifetime_arena, S8("scratch-lifetime.c"), scratch_lifetime_tokens, scratch_lifetime_parse, target_native);
    BUSTER_TEST(arguments, scratch_lifetime_tokens.diagnostic_count == 0);
    BUSTER_TEST(arguments, scratch_lifetime_parse.diagnostic_count == 0);
    BUSTER_TEST(arguments, scratch_lifetime_ir.diagnostic_count == 0);
    if (scratch_lifetime_ir.program)
    {
        IrModule* scratch_lifetime_module = &scratch_lifetime_ir.program->modules[0];
        BUSTER_TEST(arguments, scratch_lifetime_module->lowered_function_count == 2);
        BUSTER_TEST(arguments, ir_validate_canonical_module(scratch_lifetime_ir.program, scratch_lifetime_module).error == IR_VALIDATION_NONE);
    }
    bool scratch_lifetime_arena_destroyed = arena_destroy(scratch_lifetime_arena, 1);
    BUSTER_TEST(arguments, scratch_lifetime_arena_destroyed);
    TemporalArena hardening_temporary = scratch_begin(0, 0);
    CPreprocessResult hardening_tokens = c_preprocess(hardening_temporary.arena,
                                                      S8("typedef unsigned long Word;"
                                                         " typedef enum Kind"
                                                         " { KIND_VALUE = 1 } Kind;"
                                                         " typedef struct Iterator Iterator;"
                                                         " struct Iterator"
                                                         " { char const *source; Word index; };"
                                                         " typedef struct Duplicate Duplicate;"
                                                         " struct Duplicate { int value; };"
                                                         " typedef Duplicate Duplicate;"
                                                         " typedef union Constant"
                                                         " { struct { Word integer; };"
                                                         " double floating; } Constant;"
                                                         " typedef struct Pair"
                                                         " { int left; int right; } Pair;"
                                                         " typedef struct NamedInline"
                                                         " { int prefix;"
                                                         " union { int integer; double real; }"
                                                         " payload;"
                                                         " int values[2]; unsigned length;"
                                                         " struct NamedInline *pointers[2];"
                                                         " int (*callback)(int); } NamedInline;"
                                                         " typedef NamedInline NamedInline;"
                                                         " typedef struct NamedContainer"
                                                         " { NamedInline stacks; } NamedContainer;"
                                                         "\n#define SAME_LOCATION_LOCAL(name) \\\n"
                                                         " int name(int *value)"
                                                         " { int *result = value;"
                                                         " return result != 0; }\n"
                                                         "SAME_LOCATION_LOCAL(macro_local_first)\n"
                                                         "SAME_LOCATION_LOCAL(macro_local_second)\n"
                                                         " NamedInline named_zero = { 0 };"
                                                         " NamedContainer *named_global;"
                                                         " static char bound_source[7];"
                                                         " static char *bound_target["
                                                         "sizeof(bound_source) /"
                                                         " sizeof(bound_source[0])];"
                                                         " static int consume(Kind *kind)"
                                                         " { return *kind; }"
                                                         " int first(int left);"
                                                         " int second(int right);"
                                                         " int first(int actual)"
                                                         " { return actual; }"
                                                         " int second(int actual)"
                                                         " { return actual + 1; }"
                                                         " int update(Word *index, int *values)"
                                                         " { int prior = values[(*index)++];"
                                                         " return prior + (int)(--*index)"
                                                         " + consume(&(Kind){ KIND_VALUE }); }"
                                                         " int qualified("
                                                         " Iterator * restrict iterator)"
                                                         " { return *(char const *)"
                                                         " iterator->source; }"
                                                         " Word promoted(void)"
                                                         " { Constant value ="
                                                         " (Constant){ .integer = 4 };"
                                                         " return sizeof(value.integer)"
                                                         " + value.integer; }"
                                                         " int aggregate_select(int condition)"
                                                         " { Pair left = { 1, 2 };"
                                                         " Pair right = { 3, 4 };"
                                                         " Pair selected ="
                                                         " condition ? left : right;"
                                                         " return selected.left; }"
                                                         " int named_inline(NamedInline *value)"
                                                         " { return value->payload.integer; }"
                                                         " int named_global_chain(void)"
                                                         " { NamedInline *result ="
                                                         " named_global->stacks.pointers["
                                                         " --named_global->stacks.length];"
                                                         " return result != 0; }"
                                                         " int duplicate_typedef(void)"
                                                         " { Duplicate *result = 0;"
                                                         " return result != 0; }"
                                                         " unsigned named_offset(void)"
                                                         " { return __builtin_offsetof("
                                                         "NamedInline, pointers[1]); }"
                                                         " int condition_assignment(int *next)"
                                                         " { int *event;"
                                                         " while ((event = next))"
                                                         " { return event != 0; }"
                                                         " return 0; }"
                                                         " float grouped_cast_select("
                                                         "NamedInline *value,"
                                                         " unsigned index)"
                                                         " { return"
                                                         " (((float *)&value->payload.real)"
                                                         "[index] > 0.0f)"
                                                         " ? ((float *)&value->payload.real)"
                                                         "[index]"
                                                         " : ((float *)&value->payload.real)"
                                                         "[0]; }"
                                                         " unsigned object_size_bound(void)"
                                                         " { return sizeof(bound_target) /"
                                                         " sizeof(bound_target[0]); }"
                                                         " int local_string_array(void)"
                                                         " { ; const char text[] = \"a\\0b\";"
                                                         " return (int)sizeof(text) + text[2]; }"
                                                         " unsigned inline_assembly(unsigned leaf)"
                                                         " { unsigned a = leaf, b = 0, c = 0, d = 0;"
                                                         " __asm__ volatile (\"cpuid\""
                                                         " : [leaf] \"+a\"(a), \"=b\"(b),"
                                                         " \"=c\"(c), \"=d\"(d)"
                                                         " : \"c\"(0) : \"memory\", \"cc\");"
                                                         " return a ^ b ^ c ^ d; }"
                                                         " void inline_trap(void)"
                                                         " { __asm__ __volatile__(\"ud2\"); }"
                                                         " extern int global_asm_value(void);"
                                                         " __asm__(\".text\\n\""
                                                         "\".globl global_asm_value\\n\""
                                                         "\"global_asm_value:\\n\""
                                                         "\"movl $37, %eax\\n\""
                                                         "\"ret\\n\");"
                                                         " extern int asm_labeled(void)"
                                                         " __asm__(\"external_asm_name\");"
                                                         " int call_asm_labeled(void)"
                                                         " { return asm_labeled(); }"
                                                         " int no_return(void)"
                                                         " { do { __builtin_unreachable();"
                                                         " } while (0); }"),
                                                      (CPreprocessOptions){0});
    CParseResult hardening_parse = c_parse(hardening_temporary.arena, hardening_tokens);
    CIRLowerResult hardening_ir = c_lower_to_ir(hardening_temporary.arena, S8("frontend-hardening.c"), hardening_tokens, hardening_parse, target_native);
    BUSTER_TEST(arguments, hardening_tokens.diagnostic_count == 0);
    BUSTER_TEST(arguments, hardening_parse.diagnostic_count == 0);
    BUSTER_TEST(arguments, hardening_ir.diagnostic_count == 0);
    BUSTER_TEST(arguments, hardening_ir.program != 0);
    if (hardening_ir.program)
    {
        IrModule* hardening_module = &hardening_ir.program->modules[0];
        BUSTER_TEST(arguments, hardening_module->function_count == 23);
        BUSTER_TEST(arguments, hardening_module->assembly_count == 1);
        if (hardening_module->assembly_count == 1)
        {
            BUSTER_TEST(arguments, hardening_module->assemblies[0].source.length != 0);
            BUSTER_TEST(arguments, string_equal(hardening_module->assemblies[0].source, S8(".text\n"
                                                                                           ".globl global_asm_value\n"
                                                                                           "global_asm_value:\n"
                                                                                           "movl $37, %eax\n"
                                                                                           "ret\n")));
        }
        BUSTER_TEST(arguments, ir_validate_canonical_module(hardening_ir.program, hardening_module).error == IR_VALIDATION_NONE);
        u32 dereference_count = 0;
        u32 field_count = 0;
        u32 unreachable_count = 0;
        u32 inline_assembly_count = 0;
        bool found_asm_link_name = false;
        for (u32 symbol_index = 0; symbol_index < hardening_ir.program->symbols.count; symbol_index += 1)
        {
            found_asm_link_name |= string_equal(hardening_ir.program->symbols.symbols[symbol_index].link_name, S8("external_asm_name"));
        }
        for (u32 function_index = 0; function_index < hardening_module->function_count; function_index += 1)
        {
            IrFunction* function = hardening_module->functions + function_index;
            for (u32 instruction_index = 0; instruction_index < function->instruction_count; instruction_index += 1)
            {
                IrOpcode opcode = function->instructions[instruction_index].opcode;
                dereference_count += opcode == IR_OPCODE_DEREFERENCE;
                field_count += opcode == IR_OPCODE_FIELD;
                unreachable_count += opcode == IR_OPCODE_UNREACHABLE;
                inline_assembly_count += opcode == IR_OPCODE_INLINE_ASSEMBLY;
            }
        }
        BUSTER_TEST(arguments, dereference_count >= 4);
        BUSTER_TEST(arguments, field_count >= 3);
        BUSTER_TEST(arguments, unreachable_count >= 1);
        BUSTER_TEST(arguments, inline_assembly_count == 2);
        BUSTER_TEST(arguments, found_asm_link_name);
    }
    scratch_end(hardening_temporary);
    TemporalArena invalid_assembly_temporary = scratch_begin(0, 0);
    CPreprocessResult invalid_assembly_tokens =
        c_preprocess(invalid_assembly_temporary.arena, S8("__asm__(\"nop\", \"not adjacent\");"), (CPreprocessOptions){0});
    CParseResult invalid_assembly_parse = c_parse(invalid_assembly_temporary.arena, invalid_assembly_tokens);
    CIRLowerResult invalid_assembly_ir =
        c_lower_to_ir(invalid_assembly_temporary.arena, S8("invalid-global-assembly.c"), invalid_assembly_tokens, invalid_assembly_parse, target_native);
    BUSTER_TEST(arguments, invalid_assembly_tokens.diagnostic_count == 0);
    BUSTER_TEST(arguments, invalid_assembly_parse.diagnostic_count == 0);
    BUSTER_TEST(arguments, invalid_assembly_ir.diagnostic_count == 1);
    BUSTER_TEST(arguments, invalid_assembly_ir.diagnostics[0].kind == C_DIAGNOSTIC_UNSUPPORTED_SEMANTICS);
    BUSTER_TEST(arguments, invalid_assembly_ir.program != 0);
    if (invalid_assembly_ir.program)
    {
        BUSTER_TEST(arguments, invalid_assembly_ir.program->modules[0].assembly_count == 0);
    }
    scratch_end(invalid_assembly_temporary);
    TemporalArena alignas_temporary = scratch_begin(0, 0);
    CPreprocessResult alignas_tokens = c_preprocess(alignas_temporary.arena,
                                                    S8("_Alignas(64) int global_value = 1;"
                                                       " _Alignas(64) extern int redeclared;"
                                                       " _Alignas(64) int redeclared = 4;"
                                                       " struct Aligned {"
                                                       " char prefix;"
                                                       " _Alignas(32) int value;"
                                                       " };"
                                                       " int main(void) {"
                                                       " _Alignas(64) int local = 2;"
                                                       " static _Alignas(64) int saved = 3;"
                                                       " return global_value + local + saved;"
                                                       " }\n"),
                                                    (CPreprocessOptions){0});
    CParseResult alignas_parse = c_parse(alignas_temporary.arena, alignas_tokens);
    CIRLowerResult alignas_ir = c_lower_to_ir(alignas_temporary.arena, S8("alignas.c"), alignas_tokens, alignas_parse, target_native);
    BUSTER_TEST(arguments, alignas_tokens.diagnostic_count == 0);
    BUSTER_TEST(arguments, alignas_parse.diagnostic_count == 0);
    BUSTER_TEST(arguments, alignas_ir.diagnostic_count == 0);
    if (alignas_ir.program)
    {
        IrModule* alignas_module = &alignas_ir.program->modules[0];
        bool found_global_alignment = false;
        bool found_static_alignment = false;
        bool found_local_alignment = false;
        bool found_member_alignment = false;
        IrGlobal* aligned_global = 0;
        IrValue* aligned_local = 0;
        for (u32 global_index = 0; global_index < alignas_module->global_count; global_index += 1)
        {
            IrGlobal* global = alignas_module->globals + global_index;
            found_global_alignment |= global->alignment == 64;
            if (!aligned_global && global->alignment == 64)
            {
                aligned_global = global;
            }
            found_static_alignment |=
                global->alignment == 64 && ir_symbol_from_id(&alignas_ir.program->symbols, global->symbol)->linkage == IR_LINKAGE_INTERNAL;
        }
        for (u32 type_index = 0; type_index < alignas_ir.program->types.count; type_index += 1)
        {
            IrType* type = alignas_ir.program->types.types + type_index;
            found_member_alignment |= type->kind == IR_TYPE_STRUCT && type->layout.alignment == 32 && type->field_count == 2 && type->fields[1].offset == 32;
        }
        for (u32 function_index = 0; function_index < alignas_module->function_count; function_index += 1)
        {
            IrFunction* function = alignas_module->functions + function_index;
            for (u32 value_index = 0; value_index < function->value_count; value_index += 1)
            {
                found_local_alignment |= function->values[value_index].alignment == 64;
                if (!aligned_local && function->values[value_index].alignment == 64)
                {
                    aligned_local = &function->values[value_index];
                }
            }
        }
        BUSTER_TEST(arguments, found_global_alignment);
        BUSTER_TEST(arguments, found_static_alignment);
        BUSTER_TEST(arguments, found_local_alignment);
        BUSTER_TEST(arguments, found_member_alignment);
        BUSTER_TEST(arguments, ir_validate_canonical_module(alignas_ir.program, alignas_module).error == IR_VALIDATION_NONE);
        if (aligned_global)
        {
            u32 alignment = aligned_global->alignment;
            aligned_global->alignment = 3;
            BUSTER_TEST(arguments, ir_validate_canonical_module(alignas_ir.program, alignas_module).error == IR_VALIDATION_ALIGNMENT);
            aligned_global->alignment = alignment;
        }
        if (aligned_local)
        {
            u32 alignment = aligned_local->alignment;
            aligned_local->alignment = 3;
            BUSTER_TEST(arguments, ir_validate_canonical_module(alignas_ir.program, alignas_module).error == IR_VALIDATION_ALIGNMENT);
            aligned_local->alignment = alignment;
        }
    }
    scratch_end(alignas_temporary);
    TemporalArena invalid_alignas_temporary = scratch_begin(0, 0);
    CPreprocessResult invalid_alignas_tokens = c_preprocess(invalid_alignas_temporary.arena, S8("_Alignas(3) int value;\n"), (CPreprocessOptions){0});
    CParseResult invalid_alignas_parse = c_parse(invalid_alignas_temporary.arena, invalid_alignas_tokens);
    CIRLowerResult invalid_alignas_ir =
        c_lower_to_ir(invalid_alignas_temporary.arena, S8("invalid-alignas.c"), invalid_alignas_tokens, invalid_alignas_parse, target_native);
    BUSTER_TEST(arguments, invalid_alignas_tokens.diagnostic_count == 0);
    BUSTER_TEST(arguments, invalid_alignas_parse.diagnostic_count == 0);
    BUSTER_TEST(arguments, invalid_alignas_ir.diagnostic_count == 1);
    if (invalid_alignas_ir.diagnostic_count == 1)
    {
        BUSTER_TEST(arguments, invalid_alignas_ir.diagnostics[0].kind == C_DIAGNOSTIC_INVALID_ALIGNMENT);
    }
    scratch_end(invalid_alignas_temporary);
    {
        String8 invalid_redeclaration_sources[] = {
            S8("_Alignas(64) extern int value; _Alignas(32) int value = 1;\n"),
            S8("_Alignas(64) extern int value; int value = 1;\n"),
            S8("_Alignas(1) int value;\n"),
        };
        for (u32 source_index = 0; source_index < BUSTER_ARRAY_LENGTH(invalid_redeclaration_sources); source_index += 1)
        {
            TemporalArena temporary = scratch_begin(0, 0);
            CPreprocessResult invalid_tokens = c_preprocess(temporary.arena, invalid_redeclaration_sources[source_index], (CPreprocessOptions){0});
            CParseResult invalid_parse = c_parse(temporary.arena, invalid_tokens);
            CIRLowerResult invalid_ir = c_lower_to_ir(temporary.arena, S8("invalid-alignas-redeclaration.c"), invalid_tokens, invalid_parse, target_native);
            BUSTER_TEST(arguments, invalid_tokens.diagnostic_count == 0);
            BUSTER_TEST(arguments, invalid_parse.diagnostic_count == 0);
            BUSTER_TEST(arguments, invalid_ir.diagnostic_count == 1);
            if (invalid_ir.diagnostic_count == 1)
            {
                BUSTER_TEST(arguments, invalid_ir.diagnostics[0].kind == C_DIAGNOSTIC_INVALID_ALIGNMENT);
            }
            scratch_end(temporary);
        }
    }
    {
        String8 invalid_alignment_sources[] = {
            S8("_Alignas(16) typedef int Aligned;\n"),
            S8("_Alignas(16) int function(void);\n"),
            S8("int function(_Alignas(16) int value);\n"),
            S8("struct Value { _Alignas(8) unsigned field : 1; };\n"),
            S8("int function(void) { register _Alignas(16) int value; return 0; }\n"),
        };
        for (u32 source_index = 0; source_index < BUSTER_ARRAY_LENGTH(invalid_alignment_sources); source_index += 1)
        {
            TemporalArena temporary = scratch_begin(0, 0);
            CPreprocessResult invalid_tokens = c_preprocess(temporary.arena, invalid_alignment_sources[source_index], (CPreprocessOptions){0});
            CParseResult invalid_parse = c_parse(temporary.arena, invalid_tokens);
            BUSTER_TEST(arguments, invalid_tokens.diagnostic_count == 0);
            BUSTER_TEST(arguments, invalid_parse.diagnostic_count == 1);
            if (invalid_parse.diagnostic_count == 1)
            {
                BUSTER_TEST(arguments, invalid_parse.diagnostics[0].kind == C_DIAGNOSTIC_INVALID_ALIGNMENT);
            }
            scratch_end(temporary);
        }
    }
    {
        String8 valid_flexible_array_source = S8("struct Packet {"
                                                 " unsigned short tag;"
                                                 " unsigned char bytes[];"
                                                 "};\n");
        TemporalArena temporary = scratch_begin(0, 0);
        CPreprocessResult valid_tokens = c_preprocess(temporary.arena, valid_flexible_array_source, (CPreprocessOptions){0});
        CParseResult valid_parse = c_parse(temporary.arena, valid_tokens);
        CIRLowerResult valid_ir = c_lower_to_ir(temporary.arena, S8("valid-flexible-array.c"), valid_tokens, valid_parse, target_native);
        BUSTER_TEST(arguments, valid_tokens.diagnostic_count == 0);
        BUSTER_TEST(arguments, valid_parse.diagnostic_count == 0);
        BUSTER_TEST(arguments, valid_ir.diagnostic_count == 0);
        bool found_flexible_structure = false;
        if (valid_ir.program)
        {
            for (u32 type_index = 0; type_index < valid_ir.program->types.count; type_index += 1)
            {
                IrType* type = valid_ir.program->types.types + type_index;
                found_flexible_structure |= type->kind == IR_TYPE_STRUCT && type->field_count == 2 && type->layout.size == 2 && type->layout.alignment == 2 &&
                                            type->fields[1].offset == 2 &&
                                            ir_type_from_id(&valid_ir.program->types, type->fields[1].type)->kind == IR_TYPE_ARRAY &&
                                            ir_type_from_id(&valid_ir.program->types, type->fields[1].type)->element_count == 0;
            }
        }
        BUSTER_TEST(arguments, found_flexible_structure);
        scratch_end(temporary);
    }
    {
        String8 invalid_flexible_array_sources[] = {
            S8("union Packet { int tag; unsigned char bytes[]; };\n"),
            S8("struct Packet { unsigned char bytes[]; int tag; };\n"),
            S8("struct Packet { unsigned char bytes[]; };\n"),
        };
        for (u32 source_index = 0; source_index < BUSTER_ARRAY_LENGTH(invalid_flexible_array_sources); source_index += 1)
        {
            TemporalArena temporary = scratch_begin(0, 0);
            CPreprocessResult invalid_tokens = c_preprocess(temporary.arena, invalid_flexible_array_sources[source_index], (CPreprocessOptions){0});
            CParseResult invalid_parse = c_parse(temporary.arena, invalid_tokens);
            BUSTER_TEST(arguments, invalid_tokens.diagnostic_count == 0);
            BUSTER_TEST(arguments, invalid_parse.diagnostic_count == 1);
            if (invalid_parse.diagnostic_count == 1)
            {
                BUSTER_TEST(arguments, invalid_parse.diagnostics[0].kind == C_DIAGNOSTIC_INVALID_FLEXIBLE_ARRAY_MEMBER);
            }
            scratch_end(temporary);
        }
    }
    {
        TemporalArena temporary = scratch_begin(0, 0);
        Target clear_cache_target = target_native;
        clear_cache_target.cpu_arch = CPU_ARCH_AARCH64;
        clear_cache_target.os = OPERATING_SYSTEM_LINUX;
        CPreprocessResult clear_cache_tokens = c_preprocess(temporary.arena,
                                                            S8("void clear_cache(char *begin, char *end)"
                                                               " { __builtin___clear_cache(begin, end); }\n"),
                                                            (CPreprocessOptions){
                                                                .target = clear_cache_target,
                                                            });
        CParseResult clear_cache_parse = c_parse(temporary.arena, clear_cache_tokens);
        CIRLowerResult clear_cache_ir = c_lower_to_ir(temporary.arena, S8("clear-cache.c"), clear_cache_tokens, clear_cache_parse, clear_cache_target);
        BUSTER_TEST(arguments, clear_cache_tokens.diagnostic_count == 0);
        BUSTER_TEST(arguments, clear_cache_parse.diagnostic_count == 0);
        BUSTER_TEST(arguments, clear_cache_ir.diagnostic_count == 0);
        u32 clear_cache_count = 0;
        if (clear_cache_ir.program)
        {
            IrModule* module = clear_cache_ir.program->modules;
            BUSTER_TEST(arguments, ir_validate_canonical_module(clear_cache_ir.program, module).error == IR_VALIDATION_NONE);
            for (u32 function_index = 0; function_index < module->function_count; function_index += 1)
            {
                IrFunction* function = module->functions + function_index;
                for (u32 instruction_index = 0; instruction_index < function->instruction_count; instruction_index += 1)
                {
                    clear_cache_count += function->instructions[instruction_index].opcode == IR_OPCODE_CLEAR_INSTRUCTION_CACHE;
                }
            }
        }
        BUSTER_TEST(arguments, clear_cache_count == 1);
        scratch_end(temporary);
    }
    {
        TemporalArena temporary = scratch_begin(0, 0);
        CPreprocessResult atomic_tokens = c_preprocess(temporary.arena,
                                                       S8("_Atomic(int) counter;"
                                                          " const _Atomic(unsigned long) total;"
                                                          " _Atomic(int *) pointer;"
                                                          " typedef _Atomic(short) AtomicShort;"
                                                          " AtomicShort value;"
                                                          " int atomic_round_trip(void) {"
                                                          " _Atomic(int) local = 3;"
                                                          " local = local + 4;"
                                                          " local += 2;"
                                                          " local++;"
                                                          " ++local;"
                                                          " return local;"
                                                          " }\n"),
                                                       (CPreprocessOptions){0});
        CParseResult atomic_parse = c_parse(temporary.arena, atomic_tokens);
        CIRLowerResult atomic_ir = c_lower_to_ir(temporary.arena, S8("atomic-types.c"), atomic_tokens, atomic_parse, target_native);
        BUSTER_TEST(arguments, atomic_tokens.diagnostic_count == 0);
        BUSTER_TEST(arguments, atomic_parse.diagnostic_count == 0);
        BUSTER_TEST(arguments, atomic_ir.diagnostic_count == 0);
        u32 atomic_object_count = 0;
        bool found_atomic_pointer = false;
        bool found_const_atomic = false;
        for (u32 entity_index = 0; entity_index < atomic_parse.entity_count; entity_index += 1)
        {
            CEntity* entity = atomic_parse.entities + entity_index;
            if (entity->kind != C_ENTITY_OBJECT || entity->scope.value != 0 || entity->type.value >= atomic_parse.type_count)
            {
                continue;
            }
            CType* type = atomic_parse.types + entity->type.value;
            if (!type->is_atomic)
            {
                continue;
            }
            atomic_object_count += 1;
            found_atomic_pointer |= type->kind == C_TYPE_POINTER;
            found_const_atomic |= type->is_const;
        }
        BUSTER_TEST(arguments, atomic_object_count == 4);
        BUSTER_TEST(arguments, found_atomic_pointer);
        BUSTER_TEST(arguments, found_const_atomic);
        bool found_atomic_ir_type = false;
        u32 atomic_load_count = 0;
        u32 atomic_store_count = 0;
        u32 atomic_rmw_count = 0;
        if (atomic_ir.program)
        {
            for (u32 type_index = 0; type_index < atomic_ir.program->types.count; type_index += 1)
            {
                IrType* type = atomic_ir.program->types.types + type_index;
                found_atomic_ir_type |= type->is_atomic && type->unqualified_type.value < atomic_ir.program->types.count && type->layout.resolved;
            }
            IrModule* module = atomic_ir.program->modules;
            BUSTER_TEST(arguments, ir_validate_canonical_module(atomic_ir.program, module).error == IR_VALIDATION_NONE);
            for (u32 function_index = 0; function_index < module->function_count; function_index += 1)
            {
                IrFunction* function = module->functions + function_index;
                for (u32 instruction_index = 0; instruction_index < function->instruction_count; instruction_index += 1)
                {
                    IrOpcode opcode = function->instructions[instruction_index].opcode;
                    atomic_load_count += opcode == IR_OPCODE_ATOMIC_LOAD;
                    atomic_store_count += opcode == IR_OPCODE_ATOMIC_STORE;
                    atomic_rmw_count += opcode == IR_OPCODE_ATOMIC_READ_MODIFY_WRITE;
                }
            }
        }
        BUSTER_TEST(arguments, found_atomic_ir_type);
        BUSTER_TEST(arguments, atomic_load_count == 2);
        BUSTER_TEST(arguments, atomic_store_count == 2);
        BUSTER_TEST(arguments, atomic_rmw_count == 3);
        scratch_end(temporary);
    }
    {
        TemporalArena temporary = scratch_begin(0, 0);
        CPreprocessResult atomic_builtin_tokens = c_preprocess(temporary.arena,
                                                               S8("int atomic_builtin_ir(void) {"
                                                                  " _Atomic(int) value;"
                                                                  " int expected = 1;"
                                                                  " __c11_atomic_init(&value, 1);"
                                                                  " int previous = __c11_atomic_exchange("
                                                                  " &value, 2, __ATOMIC_ACQ_REL);"
                                                                  " int changed ="
                                                                  " __c11_atomic_compare_exchange_strong("
                                                                  " &value, &expected, 3,"
                                                                  " __ATOMIC_SEQ_CST,"
                                                                  " __ATOMIC_ACQUIRE);"
                                                                  " __c11_atomic_thread_fence("
                                                                  " __ATOMIC_RELEASE);"
                                                                  " __c11_atomic_signal_fence("
                                                                  " __ATOMIC_SEQ_CST);"
                                                                  " return previous + changed;"
                                                                  " }\n"),
                                                               (CPreprocessOptions){0});
        CParseResult parse = c_parse(temporary.arena, atomic_builtin_tokens);
        CIRLowerResult lowered = c_lower_to_ir(temporary.arena, S8("atomic-builtins.c"), atomic_builtin_tokens, parse, target_native);
        BUSTER_TEST(arguments, atomic_builtin_tokens.diagnostic_count == 0);
        BUSTER_TEST(arguments, parse.diagnostic_count == 0);
        BUSTER_TEST(arguments, lowered.diagnostic_count == 0);
        u32 exchange_count = 0;
        u32 compare_exchange_count = 0;
        u32 thread_fence_count = 0;
        u32 signal_fence_count = 0;
        bool compare_orders_valid = false;
        if (lowered.program)
        {
            IrModule* module = lowered.program->modules;
            BUSTER_TEST(arguments, ir_validate_canonical_module(lowered.program, module).error == IR_VALIDATION_NONE);
            for (u32 function_index = 0; function_index < module->function_count; function_index += 1)
            {
                IrFunction* function = module->functions + function_index;
                for (u32 instruction_index = 0; instruction_index < function->instruction_count; instruction_index += 1)
                {
                    IrInstruction* instruction = function->instructions + instruction_index;
                    exchange_count += instruction->opcode == IR_OPCODE_ATOMIC_READ_MODIFY_WRITE && instruction->atomic_operation == IR_ATOMIC_EXCHANGE;
                    compare_exchange_count += instruction->opcode == IR_OPCODE_ATOMIC_COMPARE_EXCHANGE;
                    if (instruction->opcode == IR_OPCODE_ATOMIC_COMPARE_EXCHANGE)
                    {
                        compare_orders_valid |=
                            instruction->memory_order == IR_MEMORY_ORDER_SEQUENTIAL && instruction->failure_memory_order == IR_MEMORY_ORDER_ACQUIRE;
                    }
                    thread_fence_count += instruction->opcode == IR_OPCODE_ATOMIC_FENCE && !instruction->atomic_signal_fence;
                    signal_fence_count += instruction->opcode == IR_OPCODE_ATOMIC_FENCE && instruction->atomic_signal_fence;
                }
            }
        }
        BUSTER_TEST(arguments, exchange_count == 1);
        BUSTER_TEST(arguments, compare_exchange_count == 1);
        BUSTER_TEST(arguments, thread_fence_count == 1);
        BUSTER_TEST(arguments, signal_fence_count == 1);
        BUSTER_TEST(arguments, compare_orders_valid);
        scratch_end(temporary);
    }
    {
        String8 invalid_atomic_sources[] = {
            S8("_Atomic(void) value;\n"),
            S8("_Atomic(int[2]) value;\n"),
            S8("_Atomic(const int) value;\n"),
            S8("_Atomic(_Atomic(int)) value;\n"),
        };
        for (u32 source_index = 0; source_index < BUSTER_ARRAY_LENGTH(invalid_atomic_sources); source_index += 1)
        {
            TemporalArena temporary = scratch_begin(0, 0);
            CPreprocessResult invalid_tokens = c_preprocess(temporary.arena, invalid_atomic_sources[source_index], (CPreprocessOptions){0});
            CParseResult invalid_parse = c_parse(temporary.arena, invalid_tokens);
            BUSTER_TEST(arguments, invalid_tokens.diagnostic_count == 0);
            BUSTER_TEST(arguments, invalid_parse.diagnostic_count == 1);
            if (invalid_parse.diagnostic_count == 1)
            {
                BUSTER_TEST(arguments, invalid_parse.diagnostics[0].kind == C_DIAGNOSTIC_INVALID_ATOMIC_TYPE);
            }
            scratch_end(temporary);
        }
    }
    {
        TemporalArena temporary = scratch_begin(0, 0);
        CPreprocessResult conflict_tokens = c_preprocess(temporary.arena,
                                                         S8("extern _Atomic(int) counter;"
                                                            " extern int counter;\n"),
                                                         (CPreprocessOptions){0});
        CParseResult conflict_parse = c_parse(temporary.arena, conflict_tokens);
        BUSTER_TEST(arguments, conflict_tokens.diagnostic_count == 0);
        BUSTER_TEST(arguments, conflict_parse.diagnostic_count == 1);
        if (conflict_parse.diagnostic_count == 1)
        {
            BUSTER_TEST(arguments, conflict_parse.diagnostics[0].kind == C_DIAGNOSTIC_CONFLICTING_DECLARATION);
        }
        scratch_end(temporary);
    }
    TemporalArena vla_temporary = scratch_begin(0, 0);
    CPreprocessResult vla_tokens = c_preprocess(vla_temporary.arena,
                                                S8("int unspecified(int count,"
                                                   " int values[*]);"
                                                   " int size(int count) {"
                                                   " int values[count];"
                                                   " values[count - 1] = 7;"
                                                   " return (int)sizeof(values)"
                                                   " + values[count - 1];"
                                                   " }"
                                                   " int loop(int count) {"
                                                   " int total = 0;"
                                                   " for (int index = 0; index < 2;"
                                                   " index += 1) {"
                                                   " int values[count];"
                                                   " values[0] = index;"
                                                   " total += values[0];"
                                                   " }"
                                                   " return total;"
                                                   " }"
                                                   " int nested(int count) {"
                                                   " int result = 0;"
                                                   " { int values[count];"
                                                   " values[0] = 9;"
                                                   " result = values[0]; }"
                                                   " return result;"
                                                   " }"
                                                   " int matrix(int rows, int columns) {"
                                                   " int values[rows][columns];"
                                                   " values[rows - 1][columns - 1] = 11;"
                                                   " return (int)sizeof(values)"
                                                   " + (int)sizeof(values[0])"
                                                   " + values[rows - 1][columns - 1];"
                                                   " }"
                                                   " int matrix_parameter("
                                                   " int rows, int columns,"
                                                   " int values[static rows][columns]) {"
                                                   " values[rows - 1][columns - 1] = 13;"
                                                   " return (int)sizeof(values[0])"
                                                   " + values[rows - 1][columns - 1];"
                                                   " }\n"),
                                                (CPreprocessOptions){0});
    CParseResult vla_parse = c_parse(vla_temporary.arena, vla_tokens);
    CIRLowerResult vla_ir = c_lower_to_ir(vla_temporary.arena, S8("vla.c"), vla_tokens, vla_parse, target_native);
    BUSTER_TEST(arguments, vla_tokens.diagnostic_count == 0);
    BUSTER_TEST(arguments, vla_parse.diagnostic_count == 0);
    BUSTER_TEST(arguments, vla_ir.diagnostic_count == 0);
    if (vla_ir.program)
    {
        IrModule* vla_module = &vla_ir.program->modules[0];
        u32 allocation_count = 0;
        u32 stack_save_count = 0;
        u32 stack_restore_count = 0;
        u32 runtime_multiply_count = 0;
        for (u32 function_index = 0; function_index < vla_module->function_count; function_index += 1)
        {
            IrFunction* function = vla_module->functions + function_index;
            for (u32 instruction_index = 0; instruction_index < function->instruction_count; instruction_index += 1)
            {
                IrInstruction* instruction = function->instructions + instruction_index;
                allocation_count += instruction->opcode == IR_OPCODE_STACK_ALLOCATE;
                stack_save_count += instruction->opcode == IR_OPCODE_STACK_SAVE;
                stack_restore_count += instruction->opcode == IR_OPCODE_STACK_RESTORE;
                runtime_multiply_count += instruction->opcode == IR_OPCODE_BINARY && instruction->binary_operation == IR_BINARY_INTEGER_MULTIPLY;
            }
        }
        BUSTER_TEST(arguments, allocation_count == 4);
        BUSTER_TEST(arguments, stack_save_count == 4);
        BUSTER_TEST(arguments, stack_restore_count == 2);
        BUSTER_TEST(arguments, runtime_multiply_count >= 7);
        BUSTER_TEST(arguments, ir_validate_canonical_module(vla_ir.program, vla_module).error == IR_VALIDATION_NONE);
    }
    scratch_end(vla_temporary);
    TemporalArena generic_temporary = scratch_begin(0, 0);
    CPreprocessResult generic_tokens = c_preprocess(generic_temporary.arena,
                                                    S8("static int selected(void)"
                                                       " { return 17; }"
                                                       " static int unselected(void)"
                                                       " { return 99; }"
                                                       " int main(void)"
                                                       " { int control = 0;"
                                                       " double floating = 1.0;"
                                                       " return _Generic(control++,"
                                                       " int: selected(),"
                                                       " default: unselected())"
                                                       " + _Generic(floating,"
                                                       " int: unselected(),"
                                                       " default: 3); }\n"),
                                                    (CPreprocessOptions){0});
    CParseResult generic_parse = c_parse(generic_temporary.arena, generic_tokens);
    CIRLowerResult generic_ir = c_lower_to_ir(generic_temporary.arena, S8("generic.c"), generic_tokens, generic_parse, target_native);
    BUSTER_TEST(arguments, generic_tokens.diagnostic_count == 0);
    BUSTER_TEST(arguments, generic_parse.diagnostic_count == 0);
    BUSTER_TEST(arguments, generic_ir.diagnostic_count == 0);
    if (generic_ir.program)
    {
        IrModule* generic_module = &generic_ir.program->modules[0];
        IrFunction* generic_main = 0;
        for (u32 function_index = 0; function_index < generic_module->function_count; function_index += 1)
        {
            if (string_equal(generic_module->functions[function_index].name, S8("main")))
            {
                generic_main = generic_module->functions + function_index;
                break;
            }
        }
        BUSTER_TEST(arguments, generic_main != 0);
        if (generic_main)
        {
            u32 generic_call_count = 0;
            u32 generic_store_count = 0;
            for (u32 instruction_index = 0; instruction_index < generic_main->instruction_count; instruction_index += 1)
            {
                IrOpcode opcode = generic_main->instructions[instruction_index].opcode;
                generic_call_count += opcode == IR_OPCODE_CALL;
                generic_store_count += opcode == IR_OPCODE_STORE;
            }
            BUSTER_TEST(arguments, generic_call_count == 1);
            BUSTER_TEST(arguments, generic_store_count == 2);
            BUSTER_TEST(arguments, ir_validate_canonical_module(generic_ir.program, generic_module).error == IR_VALIDATION_NONE);
        }
    }
    scratch_end(generic_temporary);
    {
        TemporalArena nullptr_temporary = scratch_begin(0, 0);
        CPreprocessResult nullptr_tokens = c_preprocess(nullptr_temporary.arena,
                                                        S8("typedef typeof(nullptr) nullptr_t;"
                                                           " nullptr_t value = nullptr;"
                                                           " int *pointer = nullptr;"
                                                           " int classify(void) {"
                                                           " int *a = 1 ? nullptr : pointer;"
                                                           " nullptr_t b = 1 ? nullptr : (1 - 1);"
                                                           " if (a || b || nullptr != (2 - 2)) return 0;"
                                                           " return _Generic(nullptr,"
                                                           " nullptr_t: 1, default: 0);"
                                                           " }\n"),
                                                        (CPreprocessOptions){
                                                            .dialect = C_PREPROCESS_DIALECT_C23,
                                                        });
        CParseResult nullptr_parse = c_parse(nullptr_temporary.arena, nullptr_tokens);
        CIRLowerResult nullptr_ir = c_lower_to_ir(nullptr_temporary.arena, S8("nullptr.c"), nullptr_tokens, nullptr_parse, target_native);
        BUSTER_TEST(arguments, nullptr_tokens.diagnostic_count == 0);
        BUSTER_TEST(arguments, nullptr_parse.diagnostic_count == 0);
        BUSTER_TEST(arguments, nullptr_ir.diagnostic_count == 0);
        bool found_nullptr_c_type = false;
        for (u32 type_index = 0; type_index < nullptr_parse.type_count; type_index += 1)
        {
            found_nullptr_c_type |= nullptr_parse.types[type_index].kind == C_TYPE_NULLPTR;
        }
        BUSTER_TEST(arguments, found_nullptr_c_type);
        bool found_nullptr_ir_type = false;
        if (nullptr_ir.program)
        {
            for (u32 type_index = 0; type_index < nullptr_ir.program->types.count; type_index += 1)
            {
                IrType* type = nullptr_ir.program->types.types + type_index;
                found_nullptr_ir_type |= type->kind == IR_TYPE_POINTER && type->is_nullptr && type->layout.size == 8 && type->layout.alignment == 8;
            }
            BUSTER_TEST(arguments, ir_validate_canonical_module(nullptr_ir.program, nullptr_ir.program->modules).error == IR_VALIDATION_NONE);
        }
        BUSTER_TEST(arguments, found_nullptr_ir_type);
        scratch_end(nullptr_temporary);
    }
    {
        String8 invalid_nullptr_sources[] = {
            S8("typedef typeof(nullptr) nullptr_t;"
               " int main(void) {"
               " nullptr_t value = 0;"
               " return value == nullptr;"
               " }\n"),
            S8("int main(void) {"
               " return (int)nullptr;"
               " }\n"),
            S8("int main(void) {"
               " return *nullptr;"
               " }\n"),
            S8("int main(void) {"
               " return nullptr + 1;"
               " }\n"),
            S8("int main(void) {"
               " return nullptr == 1;"
               " }\n"),
            S8("int main(void) {"
               " return 1 ? nullptr : 1;"
               " }\n"),
        };
        for (u32 case_index = 0; case_index < BUSTER_ARRAY_LENGTH(invalid_nullptr_sources); case_index += 1)
        {
            TemporalArena invalid_temporary = scratch_begin(0, 0);
            CPreprocessResult invalid_tokens = c_preprocess(invalid_temporary.arena, invalid_nullptr_sources[case_index],
                                                            (CPreprocessOptions){
                                                                .dialect = C_PREPROCESS_DIALECT_C23,
                                                            });
            CParseResult invalid_parse = c_parse(invalid_temporary.arena, invalid_tokens);
            CIRLowerResult invalid_ir = c_lower_to_ir(invalid_temporary.arena, S8("invalid-nullptr.c"), invalid_tokens, invalid_parse, target_native);
            BUSTER_TEST(arguments, invalid_tokens.diagnostic_count == 0);
            BUSTER_TEST(arguments, invalid_parse.diagnostic_count == 0);
            BUSTER_TEST(arguments, invalid_ir.diagnostic_count == 1);
            if (invalid_ir.diagnostic_count == 1)
            {
                BUSTER_TEST(arguments, invalid_ir.diagnostics[0].kind == C_DIAGNOSTIC_UNSUPPORTED_SEMANTICS);
            }
            scratch_end(invalid_temporary);
        }
    }
    {
        TemporalArena c17_nullptr_temporary = scratch_begin(0, 0);
        CPreprocessResult c17_nullptr_tokens = c_preprocess(c17_nullptr_temporary.arena,
                                                            S8("int nullptr = 3;"
                                                               " int main(void) {"
                                                               " return nullptr - 3;"
                                                               " }\n"),
                                                            (CPreprocessOptions){
                                                                .dialect = C_PREPROCESS_DIALECT_C17,
                                                            });
        CParseResult c17_nullptr_parse = c_parse(c17_nullptr_temporary.arena, c17_nullptr_tokens);
        CIRLowerResult c17_nullptr_ir =
            c_lower_to_ir(c17_nullptr_temporary.arena, S8("c17-nullptr-identifier.c"), c17_nullptr_tokens, c17_nullptr_parse, target_native);
        BUSTER_TEST(arguments, c17_nullptr_tokens.diagnostic_count == 0);
        BUSTER_TEST(arguments, c17_nullptr_parse.diagnostic_count == 0);
        BUSTER_TEST(arguments, c17_nullptr_ir.diagnostic_count == 0);
        if (c17_nullptr_ir.program)
        {
            BUSTER_TEST(arguments, ir_validate_canonical_module(c17_nullptr_ir.program, c17_nullptr_ir.program->modules).error == IR_VALIDATION_NONE);
        }
        scratch_end(c17_nullptr_temporary);
    }
    {
        TemporalArena constexpr_temporary = scratch_begin(0, 0);
        CPreprocessResult constexpr_tokens = c_preprocess(constexpr_temporary.arena,
                                                          S8("struct Pair { int x; int y; };"
                                                             " constexpr int count = 4;"
                                                             " constexpr int next = count + 1;"
                                                             " constexpr double exact = 1.5;"
                                                             " constexpr int values[] = { 1, 2, 3, 4 };"
                                                             " constexpr struct Pair pair = { count, next };"
                                                             " constexpr int *nothing = nullptr;"
                                                             " static_assert(next == 5);"
                                                             " int sized[count];"
                                                             " int main(void) {"
                                                             " constexpr int local = next + 1;"
                                                             " int automatic[local];"
                                                             " automatic[0] = local;"
                                                             " return automatic[0] == 6 &&"
                                                             " values[3] == 4 && pair.y == 5 &&"
                                                             " nothing == nullptr ? 0 : 1;"
                                                             " }\n"),
                                                          (CPreprocessOptions){
                                                              .dialect = C_PREPROCESS_DIALECT_C23,
                                                          });
        CParseResult constexpr_parse = c_parse(constexpr_temporary.arena, constexpr_tokens);
        CIRLowerResult constexpr_ir = c_lower_to_ir(constexpr_temporary.arena, S8("constexpr.c"), constexpr_tokens, constexpr_parse, target_native);
        BUSTER_TEST(arguments, constexpr_tokens.diagnostic_count == 0);
        BUSTER_TEST(arguments, constexpr_parse.diagnostic_count == 0);
        BUSTER_TEST(arguments, constexpr_ir.diagnostic_count == 0);
        u32 constexpr_entity_count = 0;
        bool found_count = false;
        bool found_next = false;
        bool found_local = false;
        for (u32 entity_index = 0; entity_index < constexpr_parse.entity_count; entity_index += 1)
        {
            CEntity* entity = constexpr_parse.entities + entity_index;
            if (!entity->is_constexpr)
            {
                continue;
            }
            constexpr_entity_count += 1;
            BUSTER_TEST(arguments, entity->type.value < constexpr_parse.type_count);
            if (entity->type.value < constexpr_parse.type_count)
            {
                BUSTER_TEST(arguments, constexpr_parse.types[entity->type.value].is_const);
            }
            found_count |=
                string_equal(entity->name, S8("count")) && entity->has_constant_value && !entity->constant_is_negative && entity->constant_value == 4;
            found_next |= string_equal(entity->name, S8("next")) && entity->has_constant_value && entity->constant_value == 5;
            found_local |= string_equal(entity->name, S8("local")) && entity->has_constant_value && entity->constant_value == 6;
        }
        BUSTER_TEST(arguments, constexpr_entity_count == 7);
        BUSTER_TEST(arguments, found_count);
        BUSTER_TEST(arguments, found_next);
        BUSTER_TEST(arguments, found_local);
        if (constexpr_ir.program)
        {
            IrModule* module = constexpr_ir.program->modules;
            u32 read_only_globals = 0;
            u32 internal_globals = 0;
            for (u32 global_index = 0; global_index < module->global_count; global_index += 1)
            {
                IrGlobal* global = module->globals + global_index;
                IrSymbol* symbol = ir_symbol_from_id(&constexpr_ir.program->symbols, global->symbol);
                bool named_constexpr =
                    symbol && (string_equal(symbol->name, S8("count")) || string_equal(symbol->name, S8("next")) || string_equal(symbol->name, S8("exact")) ||
                               string_equal(symbol->name, S8("values")) || string_equal(symbol->name, S8("pair")) || string_equal(symbol->name, S8("nothing")));
                if (!named_constexpr)
                {
                    continue;
                }
                read_only_globals += global->is_read_only;
                internal_globals += symbol->linkage == IR_LINKAGE_INTERNAL;
            }
            BUSTER_TEST(arguments, read_only_globals == 6);
            BUSTER_TEST(arguments, internal_globals == 6);
            BUSTER_TEST(arguments, ir_validate_canonical_module(constexpr_ir.program, module).error == IR_VALIDATION_NONE);
        }
        scratch_end(constexpr_temporary);
    }
    {
        TemporalArena c17_constexpr_temporary = scratch_begin(0, 0);
        CPreprocessResult c17_constexpr_tokens = c_preprocess(c17_constexpr_temporary.arena,
                                                              S8("int constexpr = 3;"
                                                                 " int main(void) {"
                                                                 " return constexpr - 3;"
                                                                 " }\n"),
                                                              (CPreprocessOptions){
                                                                  .dialect = C_PREPROCESS_DIALECT_C17,
                                                              });
        CParseResult c17_constexpr_parse = c_parse(c17_constexpr_temporary.arena, c17_constexpr_tokens);
        CIRLowerResult c17_constexpr_ir =
            c_lower_to_ir(c17_constexpr_temporary.arena, S8("c17-constexpr-identifier.c"), c17_constexpr_tokens, c17_constexpr_parse, target_native);
        BUSTER_TEST(arguments, c17_constexpr_tokens.diagnostic_count == 0);
        BUSTER_TEST(arguments, c17_constexpr_parse.diagnostic_count == 0);
        BUSTER_TEST(arguments, c17_constexpr_ir.diagnostic_count == 0);
        scratch_end(c17_constexpr_temporary);
    }
    {
        String8 invalid_constexpr_sources[] = {
            S8("constexpr int missing;\n"),
            S8("constexpr int function(void)"
               " { return 0; }\n"),
            S8("extern constexpr int external = 1;\n"),
            S8("_Thread_local constexpr int threaded = 1;\n"),
            S8("constexpr volatile int value = 1;\n"),
            S8("constexpr _Atomic(int) value = 1;\n"),
            S8("void f(int n) {"
               " constexpr int values[n] = { 1 };"
               " }\n"),
            S8("constexpr unsigned char value = 256;\n"),
            S8("constexpr unsigned long long value = -1;\n"),
            S8("constexpr _Bool value = 2;\n"),
            S8("constexpr int *pointer = (int *)1;\n"),
            S8("void f(int runtime) {"
               " constexpr int value = runtime;"
               " }\n"),
        };
        for (u32 case_index = 0; case_index < BUSTER_ARRAY_LENGTH(invalid_constexpr_sources); case_index += 1)
        {
            TemporalArena invalid_temporary = scratch_begin(0, 0);
            CPreprocessResult invalid_tokens = c_preprocess(invalid_temporary.arena, invalid_constexpr_sources[case_index],
                                                            (CPreprocessOptions){
                                                                .dialect = C_PREPROCESS_DIALECT_C23,
                                                            });
            CParseResult invalid_parse = c_parse(invalid_temporary.arena, invalid_tokens);
            bool found_constexpr_diagnostic = false;
            for (u32 diagnostic_index = 0; diagnostic_index < invalid_parse.diagnostic_count; diagnostic_index += 1)
            {
                found_constexpr_diagnostic |= invalid_parse.diagnostics[diagnostic_index].kind == C_DIAGNOSTIC_INVALID_CONSTEXPR;
            }
            BUSTER_TEST(arguments, invalid_tokens.diagnostic_count == 0);
            BUSTER_TEST(arguments, found_constexpr_diagnostic);
            scratch_end(invalid_temporary);
        }
    }
    {
        String8 invalid_constexpr_ir_sources[] = {
            S8("constexpr float value = 0.1;\n"),
            S8("static int target;"
               " struct Pointer { int *value; };"
               " constexpr struct Pointer pointer ="
               " { &target };\n"),
        };
        for (u32 case_index = 0; case_index < BUSTER_ARRAY_LENGTH(invalid_constexpr_ir_sources); case_index += 1)
        {
            TemporalArena invalid_temporary = scratch_begin(0, 0);
            CPreprocessResult invalid_tokens = c_preprocess(invalid_temporary.arena, invalid_constexpr_ir_sources[case_index],
                                                            (CPreprocessOptions){
                                                                .dialect = C_PREPROCESS_DIALECT_C23,
                                                            });
            CParseResult invalid_parse = c_parse(invalid_temporary.arena, invalid_tokens);
            CIRLowerResult invalid_ir = c_lower_to_ir(invalid_temporary.arena, S8("invalid-constexpr.c"), invalid_tokens, invalid_parse, target_native);
            BUSTER_TEST(arguments, invalid_tokens.diagnostic_count == 0);
            BUSTER_TEST(arguments, invalid_parse.diagnostic_count == 0);
            BUSTER_TEST(arguments, invalid_ir.diagnostic_count == 1);
            if (invalid_ir.diagnostic_count == 1)
            {
                BUSTER_TEST(arguments, invalid_ir.diagnostics[0].kind == C_DIAGNOSTIC_INVALID_CONSTEXPR);
            }
            scratch_end(invalid_temporary);
        }
    }
    {
        TemporalArena float_temporary = scratch_begin(0, 0);
        CPreprocessResult float_tokens = c_preprocess(float_temporary.arena,
                                                      S8("constexpr float value = 0.1f;"
                                                         " int main(void) {"
                                                         " return value == 0.1f ? 0 : 1;"
                                                         " }\n"),
                                                      (CPreprocessOptions){
                                                          .dialect = C_PREPROCESS_DIALECT_C23,
                                                      });
        CParseResult float_parse = c_parse(float_temporary.arena, float_tokens);
        CIRLowerResult float_ir = c_lower_to_ir(float_temporary.arena, S8("constexpr-float.c"), float_tokens, float_parse, target_native);
        BUSTER_TEST(arguments, float_tokens.diagnostic_count == 0);
        BUSTER_TEST(arguments, float_parse.diagnostic_count == 0);
        BUSTER_TEST(arguments, float_ir.diagnostic_count == 0);
        if (float_ir.program)
        {
            BUSTER_TEST(arguments, ir_validate_canonical_module(float_ir.program, float_ir.program->modules).error == IR_VALIDATION_NONE);
        }
        scratch_end(float_temporary);
    }
    {
        String8 constexpr_mutations[] = {
            S8("constexpr int value = 1;"
               " int main(void) {"
               " value = 2; return value;"
               " }\n"),
            S8("constexpr int values[] = { 1 };"
               " int main(void) {"
               " values[0] += 1; return values[0];"
               " }\n"),
            S8("struct Pair { int x; };"
               " constexpr struct Pair pair = { 1 };"
               " int main(void) {"
               " pair.x++; return pair.x;"
               " }\n"),
            S8("int main(void) {"
               " constexpr int local = 1;"
               " ++local; return local;"
               " }\n"),
            S8("constexpr int value = 1;"
               " int main(void) {"
               " int const *pointer = &value;"
               " *pointer = 2; return value;"
               " }\n"),
        };
        for (u32 case_index = 0; case_index < BUSTER_ARRAY_LENGTH(constexpr_mutations); case_index += 1)
        {
            TemporalArena mutation_temporary = scratch_begin(0, 0);
            CPreprocessResult mutation_tokens = c_preprocess(mutation_temporary.arena, constexpr_mutations[case_index],
                                                             (CPreprocessOptions){
                                                                 .dialect = C_PREPROCESS_DIALECT_C23,
                                                             });
            CParseResult mutation_parse = c_parse(mutation_temporary.arena, mutation_tokens);
            CIRLowerResult mutation_ir = c_lower_to_ir(mutation_temporary.arena, S8("constexpr-mutation.c"), mutation_tokens, mutation_parse, target_native);
            BUSTER_TEST(arguments, mutation_tokens.diagnostic_count == 0);
            BUSTER_TEST(arguments, mutation_parse.diagnostic_count == 0);
            BUSTER_TEST(arguments, mutation_ir.diagnostic_count == 1);
            scratch_end(mutation_temporary);
        }
    }
    {
        String8 c23_reserved_identifiers[] = {
            S8("nullptr"), S8("true"), S8("false"), S8("constexpr"), S8("typeof_unqual"),
        };
        for (u32 case_index = 0; case_index < BUSTER_ARRAY_LENGTH(c23_reserved_identifiers); case_index += 1)
        {
            TemporalArena reserved_temporary = scratch_begin(0, 0);
            String8 source = string_format(reserved_temporary.arena, S8("int {S8} = 0;\n"), c23_reserved_identifiers[case_index]);
            CPreprocessResult reserved_tokens = c_preprocess(reserved_temporary.arena, source,
                                                             (CPreprocessOptions){
                                                                 .dialect = C_PREPROCESS_DIALECT_C23,
                                                             });
            CParseResult reserved_parse = c_parse(reserved_temporary.arena, reserved_tokens);
            BUSTER_TEST(arguments, reserved_tokens.diagnostic_count == 0);
            BUSTER_TEST(arguments, reserved_parse.diagnostic_count != 0);
            scratch_end(reserved_temporary);
        }
    }
    struct
    {
        String8 source;
        String8 message;
    } invalid_generic_cases[] = {
        {
            S8("int main(void) {"
               " return _Generic(1,"
               " default: 1, default: 2); }\n"),
            S8("in function 'main': _Generic selection has more than one default association"),
        },
        {
            S8("int main(void) {"
               " return _Generic(1,"
               " int: 1, signed int: 2); }\n"),
            S8("in function 'main': _Generic selection has multiple compatible type associations"),
        },
        {
            S8("int main(void) {"
               " return _Generic(1.0, int: 1); }\n"),
            S8("in function 'main': _Generic controlling type is not compatible with any association and no default was provided"),
        },
        {
            S8("int main(void) {"
               " return _Generic(1, void: 1,"
               " default: 2); }\n"),
            S8("in function 'main': _Generic association requires a complete object type"),
        },
    };
    for (u32 case_index = 0; case_index < BUSTER_ARRAY_LENGTH(invalid_generic_cases); case_index += 1)
    {
        TemporalArena invalid_generic_temporary = scratch_begin(0, 0);
        CPreprocessResult invalid_generic_tokens =
            c_preprocess(invalid_generic_temporary.arena, invalid_generic_cases[case_index].source, (CPreprocessOptions){0});
        CParseResult invalid_generic_parse = c_parse(invalid_generic_temporary.arena, invalid_generic_tokens);
        CIRLowerResult invalid_generic_ir =
            c_lower_to_ir(invalid_generic_temporary.arena, S8("invalid-generic.c"), invalid_generic_tokens, invalid_generic_parse, target_native);
        BUSTER_TEST(arguments, invalid_generic_tokens.diagnostic_count == 0);
        BUSTER_TEST(arguments, invalid_generic_parse.diagnostic_count == 0);
        BUSTER_TEST(arguments, invalid_generic_ir.diagnostic_count == 1);
        if (invalid_generic_ir.diagnostic_count == 1)
        {
            BUSTER_STRING_TEST(arguments, invalid_generic_ir.diagnostics[0].message, invalid_generic_cases[case_index].message);
        }
        scratch_end(invalid_generic_temporary);
    }
    {
        TemporalArena direct_ir_temporary = scratch_begin(0, 0);
        CPreprocessResult direct_ir_tokens =
            c_preprocess(direct_ir_temporary.arena,
                         S8("typedef unsigned long Word;\n"
                            "typedef unsigned char Byte;\n"
                            "enum { BASE_INDEX = 3 };\n"
                            "struct Pair { char prefix; int values[3]; };\n"
                            "_Alignas(sizeof(int) * 2) static int aligned = (unsigned long)7;\n"
                            "static const int base = 4;\n"
                            "static int scalar = (unsigned long)(base + sizeof(Byte));\n"
                            "static int table[sizeof(struct Pair) / sizeof(int)] ="
                            " { [BASE_INDEX] = (int)(sizeof(Word) / sizeof(Byte)) };\n"
                            "static const int primitive_width = sizeof(long long) + sizeof(signed char) +"
                            " sizeof(_Bool) + sizeof(double);\n"
                            "static const int compound_width = sizeof((int[3]){ 1, 2, 3 });\n"
                            "static int compound_values[3] = { [1 + 1] = 9 };\n"
                            "int inspect(void) {\n"
                            "    struct Pair pair = { .values = { [1] = 5 } };\n"
                            "    int local[sizeof((pair).values) / sizeof((pair).values[0])];\n"
                            "    switch (sizeof((pair).values) / sizeof((pair).values[0])) {\n"
                            "        case BASE_INDEX: return local[1] + table[BASE_INDEX] + scalar + aligned;\n"
                            "        default: return compound_width + compound_values[2];\n"
                            "    }\n"
                            "}\n"),
                         (CPreprocessOptions){0});
        CParseResult direct_ir_parse = c_parse(direct_ir_temporary.arena, direct_ir_tokens);
        CIRLowerResult direct_ir = c_lower_to_ir(direct_ir_temporary.arena, S8("direct-ir-regression.c"), direct_ir_tokens, direct_ir_parse, target_native);
        BUSTER_TEST(arguments, direct_ir_tokens.diagnostic_count == 0);
        BUSTER_TEST(arguments, direct_ir_parse.diagnostic_count == 0);
        BUSTER_TEST(arguments, direct_ir.diagnostic_count == 0);
        BUSTER_TEST(arguments, direct_ir.program != 0);
        if (direct_ir.program)
        {
            IrModule* module = &direct_ir.program->modules[0];
            bool found_aligned = false;
            bool found_base = false;
            bool found_scalar = false;
            bool found_table = false;
            bool found_primitive_width = false;
            bool found_compound_width = false;
            bool found_compound_values = false;
            u32 expected_word_bytes = target_native.os == OPERATING_SYSTEM_WINDOWS ? 4 : 8;
            BUSTER_TEST(arguments, module->global_count == 7);
            BUSTER_TEST(arguments, module->function_count == 1);
            for (u32 global_index = 0; global_index < module->global_count; global_index += 1)
            {
                IrGlobal* global = module->globals + global_index;
                IrSymbol* symbol = ir_symbol_from_id(&direct_ir.program->symbols, global->symbol);
                if (!symbol)
                {
                    continue;
                }
                if (string_equal(symbol->name, S8("aligned")))
                {
                    found_aligned = true;
                    BUSTER_TEST(arguments, global->alignment == 8);
                    BUSTER_TEST(arguments, global->initializer_kind == IR_GLOBAL_INITIALIZER_INTEGER);
                    BUSTER_TEST(arguments, global->initializer_bits == 7);
                }
                else if (string_equal(symbol->name, S8("base")))
                {
                    found_base = true;
                    BUSTER_TEST(arguments, global->is_read_only);
                    BUSTER_TEST(arguments, global->initializer_kind == IR_GLOBAL_INITIALIZER_INTEGER);
                    BUSTER_TEST(arguments, global->initializer_bits == 4);
                }
                else if (string_equal(symbol->name, S8("scalar")))
                {
                    found_scalar = true;
                    BUSTER_TEST(arguments, global->initializer_kind == IR_GLOBAL_INITIALIZER_INTEGER);
                    BUSTER_TEST(arguments, global->initializer_bits == 5);
                }
                else if (string_equal(symbol->name, S8("table")))
                {
                    found_table = true;
                    BUSTER_TEST(arguments, global->initializer_kind == IR_GLOBAL_INITIALIZER_BYTES);
                    BUSTER_TEST(arguments, global->bytes.length == 16);
                    if (global->bytes.pointer && global->bytes.length >= 4 * sizeof(u32))
                    {
                        u32 value = 0;
                        memcpy(&value, global->bytes.pointer + 3 * sizeof(value), sizeof(value));
                        BUSTER_TEST(arguments, value == expected_word_bytes);
                    }
                    else
                    {
                        BUSTER_TEST(arguments, false);
                    }
                }
                else if (string_equal(symbol->name, S8("primitive_width")))
                {
                    found_primitive_width = true;
                    BUSTER_TEST(arguments, global->is_read_only);
                    BUSTER_TEST(arguments, global->initializer_kind == IR_GLOBAL_INITIALIZER_INTEGER);
                    BUSTER_TEST(arguments, global->initializer_bits == 18);
                }
                else if (string_equal(symbol->name, S8("compound_width")))
                {
                    found_compound_width = true;
                    BUSTER_TEST(arguments, global->is_read_only);
                    BUSTER_TEST(arguments, global->initializer_kind == IR_GLOBAL_INITIALIZER_INTEGER);
                    BUSTER_TEST(arguments, global->initializer_bits == 12);
                }
                else if (string_equal(symbol->name, S8("compound_values")))
                {
                    found_compound_values = true;
                    BUSTER_TEST(arguments, global->initializer_kind == IR_GLOBAL_INITIALIZER_BYTES);
                    BUSTER_TEST(arguments, global->bytes.length == 12);
                    if (global->bytes.pointer && global->bytes.length >= 3 * sizeof(u32))
                    {
                        u32 value = 0;
                        memcpy(&value, global->bytes.pointer + 2 * sizeof(value), sizeof(value));
                        BUSTER_TEST(arguments, value == 9);
                    }
                    else
                    {
                        BUSTER_TEST(arguments, false);
                    }
                }
            }
            BUSTER_TEST(arguments, found_aligned);
            BUSTER_TEST(arguments, found_base);
            BUSTER_TEST(arguments, found_scalar);
            BUSTER_TEST(arguments, found_table);
            BUSTER_TEST(arguments, found_primitive_width);
            BUSTER_TEST(arguments, found_compound_width);
            BUSTER_TEST(arguments, found_compound_values);
            IrFunction* inspect = module->functions;
            BUSTER_TEST(arguments, inspect->state == IR_FUNCTION_LOWERED);
            BUSTER_TEST(arguments, inspect->local_count >= 2);
            BUSTER_TEST(arguments, ir_validate_canonical_module(direct_ir.program, module).error == IR_VALIDATION_NONE);
        }
        scratch_end(direct_ir_temporary);
    }
    enum
    {
        C_IR_NESTED_CALL_STRESS_DEPTH = 256,
    };
    TemporalArena nested_temporary = scratch_begin(0, 0);
    String8 nested_prefix = S8("static int identity(int value)"
                               " { return value; }"
                               " int main(void) { return ");
    String8 nested_call = S8("identity(");
    String8 nested_suffix = S8("1; }");
    u64 nested_source_length = nested_prefix.length + (u64)C_IR_NESTED_CALL_STRESS_DEPTH * (nested_call.length + 1) + nested_suffix.length;
    char8* nested_source_pointer = arena_allocate(nested_temporary.arena, char8, nested_source_length);
    u64 nested_source_at = 0;
    memcpy(nested_source_pointer + nested_source_at, nested_prefix.pointer, nested_prefix.length);
    nested_source_at += nested_prefix.length;
    for (u32 depth = 0; depth < C_IR_NESTED_CALL_STRESS_DEPTH; depth += 1)
    {
        memcpy(nested_source_pointer + nested_source_at, nested_call.pointer, nested_call.length);
        nested_source_at += nested_call.length;
    }
    nested_source_pointer[nested_source_at++] = '1';
    for (u32 depth = 0; depth < C_IR_NESTED_CALL_STRESS_DEPTH; depth += 1)
    {
        nested_source_pointer[nested_source_at++] = ')';
    }
    memcpy(nested_source_pointer + nested_source_at, nested_suffix.pointer + 1, nested_suffix.length - 1);
    nested_source_at += nested_suffix.length - 1;
    BUSTER_TEST(arguments, nested_source_at == nested_source_length);
    String8 nested_source = {
        .pointer = nested_source_pointer,
        .length = nested_source_length,
    };
    CPreprocessResult nested_tokens = c_preprocess(nested_temporary.arena, nested_source, (CPreprocessOptions){0});
    CParseResult nested_parse = c_parse(nested_temporary.arena, nested_tokens);
    CIRLowerResult nested_ir = c_lower_to_ir(nested_temporary.arena, S8("nested-calls.c"), nested_tokens, nested_parse, target_native);
    BUSTER_TEST(arguments, nested_ir.diagnostic_count == 0);
    if (nested_ir.program)
    {
        IrFunction* nested_main = nested_ir.program->modules[0].functions + 1;
        u32 nested_call_count = 0;
        for (u32 instruction_index = 0; instruction_index < nested_main->instruction_count; instruction_index += 1)
        {
            nested_call_count += nested_main->instructions[instruction_index].opcode == IR_OPCODE_CALL;
        }
        BUSTER_TEST(arguments, nested_call_count == C_IR_NESTED_CALL_STRESS_DEPTH);
        BUSTER_TEST(arguments, ir_validate_canonical_module(nested_ir.program, &nested_ir.program->modules[0]).error == IR_VALIDATION_NONE);
    }
    scratch_end(nested_temporary);
    return result;
}
