#pragma once

#include <buster/tests/test.h>
#include <buster/lib/compiler/frontend/buster/parser.h>
#include <buster/lib/file.h>
#include <buster/lib/string.h>

#if BUSTER_INCLUDE_TESTS
BUSTER_F_DECL String8 ast_expression_to_string(Arena* arena, AstExpression expression);
BUSTER_F_DECL StringLiteralParsing parse_string_literal(Arena* arena, String8 spelling);
BUSTER_F_DECL String8 string_from_token_id(TokenIdEnum id);
BUSTER_V_DECL ParserFileTestCase parser_file_test_cases[PARSER_FILE_TEST_CASE_COUNT];

BUSTER_F_DECL UnitTestResult parser_tokenizer_tests(UnitTestArguments* arguments);
BUSTER_F_DECL UnitTestResult parser_expression_tests(UnitTestArguments* arguments);
BUSTER_F_DECL UnitTestResult parser_result_tests(UnitTestArguments* arguments);
BUSTER_F_DECL UnitTestResult parser_file_tests(UnitTestArguments* arguments);
#endif
