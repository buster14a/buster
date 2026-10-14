#!/usr/bin/env python3
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

from check_c_lowering_provenance import lowering_sources, violations


class LoweringProvenanceTests(unittest.TestCase):
    def test_owned_nodes_and_source_anchors(self):
        source = '''
            CAst const* ast = builder->syntax_tree;
            u32 node = c_ast_child_at(ast, call, 0);
            CSourceAnchor anchor = c_source_anchor_at(&builder->preprocess, ast->tokens[node]);
            String8 text = c_source_anchor_spelling(&builder->preprocess, anchor);
            IrSourceRange source = {.offset = anchor.offset, .length = anchor.length};
            CPreprocessResult const* source_view = &builder->preprocess;
            u32 selected = source_view ? ast->tokens[node] : 0;
            u32 other = c_source_anchor_spelling(source_view, anchor).length ? ast->tokens[node] : selected;
            CType const* type = parse->types + facts->types[node];
            if (type->kind == C_TYPE_POINTER) use(type->element_type);
        '''
        self.assertEqual([], violations(source))

    def test_grammar_reads_are_rejected(self):
        cases = [
            "CToken token = source_anchor(); if (token.kind == 1) consume();",
            "if (builder->preprocess.tokens[i].kind == 1) consume();",
            "CPreprocessResult const* pp; if (pp->tokens[i].punctuator) consume();",
            "CPreprocessResult units[2]; consume(units[0].tokens[i].kind);",
            "CPreprocessResult const* pp; consume((*pp).tokens[i].kind);",
            "CPreprocessResult original; auto copy = original; consume(copy.tokens[i].kind);",
            "CPreprocessResult first, second; consume(second.tokens[i].kind);",
            "consume(unit.expanded_stream.tokens[i].kind);",
            "CAst const* ast; consume(ast->expanded_stream.tokens[i].kind);",
            "consume(builder->syntax_tree->expanded_stream.tokens[i].kind);",
            "auto copy = fetch_expansion(); consume(copy.tokens[i].kind);",
            "consume(c_token_is_punctuator(&token, C_PUNCTUATOR_COMMA));",
            "consume(c_punctuator_in_set(value, set));",
            "consume(c_preprocess_token_site(&preprocess, value));",
            "consume(c_preprocess_token_shape_at(shapes, &preprocess, i));",
            "consume(c_parse_matching_delimiter(preprocess, start, end, a, b));",
            "consume(c_ast_facts_locate(facts, &preprocess, start, end));",
            "consume(c_ast_spans_find(spans, start, end));",
            "consume(c_ast_spans_locate(spans, start, end, 0));",
            "consume(c_ir_tree_range_node(builder, start, end));",
            "consume(c_ir_type_name_node_at(builder, start, end));",
            "consume(c_ir_group_type_name(builder, start, end));",
            "consume(c_ir_compound_literal_type(builder, start, end));",
            "consume(c_ir_sizeof_operand_type_attempt(builder, start, end));",
            "consume(c_ir_predict_expression_type(builder, start, end));",
            "consume(c_ir_conditional_node(builder, start, end));",
            "consume(c_ir_body_nodes_size(ast, &function_count));",
            "consume(c_ir_body_nodes_build(arena, ast, function_count));",
            "consume(c_ir_body_nodes_find(keys, count, declaration.body_start - 1));",
            "CIrBodyNodes old_body_index;",
            "consume(c_ir_core_chain_unit(builder, state, node));",
            "if (state->chain_first == spans->first[node]) return state->chain_unit;",
            "consume(c_ast_build(arena, preprocess, options));",
            "consume(c_ast_spans_build(ast, preprocess, matches));",
            "consume(c_parse_type_layout(&parse, preprocess, start, end));",
            "consume(c_parse_static_assert_diagnostic_message(arena, preprocess, start, end));",
            "consume(c_parse_lookup_entity(&parse, scope, name));",
            "consume(c_parser_parse_function_body(preprocess, start, end));",
            "consume(c_type_parse_source_step(machine));",
            "CTypeParseMachine legacy_parser;",
            "CTokenPositionIndex const* delimiter_index;",
            "consume(c_semantic_vendor_selector(arena, &parse, preprocess, scope, start, end));",
            "consume(c_semantic_unreviewed_parser(preprocess, start, end));",
            "consume(c_ir_primitive_type_kind(preprocess, start, end));",
            "#if 0\nCToken token;\n#endif",
            "CTok\\\nen token;",
        ]
        for source in cases:
            with self.subTest(source=source):
                self.assertTrue(violations(source))

    def test_comments_and_literals_are_not_code(self):
        source = '''
            #include <buster/lib/compiler/frontend/c/c_parse_internal.h>
            // CToken c_token_is_punctuator preprocess.tokens[0].kind
            /* C_TOKEN_IDENTIFIER */
            String8 text = S8("CToken C_TOKEN_IDENTIFIER c_parse_matching_delimiter");
            char slash = '/'; char quote = '\\'';
        '''
        self.assertEqual([], violations(source))

    def test_line_numbers_follow_multiline_comments(self):
        found = violations("/* a\n b */\n\nCToken x;")
        self.assertEqual([("CToken", 4)], [(token.spelling, token.line) for token in found])
        found = violations("/* a\\\n b */\nCTok\\\nen x;\nCToken y;")
        self.assertEqual([("CToken", 3), ("CToken", 5)], [(token.spelling, token.line) for token in found])

    def test_finished_model_tables_are_allowed(self):
        source = '''
            CAst const* ast = parse.ast;
            c_parse_builtin_layout(target, C_TYPE_INT);
            c_parse_scope_for_token(&parse, root, ast->tokens[node]);
            c_parse_type_alignment(&parse, type);
            c_parse_declaration_binding_fact(&parse, declaration.binding_plus_one);
            c_parse_aggregate_attributes(&parse, type);
            c_parse_lookup_existing_entity(&parse, scope, name);
            c_parse_index_scope_children(&parse, scratch);
            c_parse_index_declarations(&parse, scratch);
            c_type_parse_buffer_size_add(&size, count, element_size, alignment);
            c_semantic_integer_rank(C_TYPE_INT);
            c_semantic_vendor_immediate_limit(name, argument);
            c_semantic_source_line(&preprocess, ast->tokens[node]);
        '''
        self.assertEqual([], violations(source))

    def test_included_lowering_read_fails_the_command(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            directory = root / "src/buster/lib/compiler/frontend/c"
            directory.mkdir(parents=True)
            (directory / "c_vendor_builtin.c").write_text("CToken semantic_only;\n", encoding="utf-8")
            root_includes = ['#include "c_vendor_lowering.c"\n',
                             '#include <buster/lib/compiler/frontend/c/c_vendor_lowering.c>\n',
                             '#inc\\\nlude <buster/lib/compiler/frontend/c/c_vendor_lowering.c>\n']
            nested_includes = ['#include "c_vendor_private.c"\n',
                               '#include /* implementation */ <buster/lib/compiler/frontend/c/c_vendor_private.c>\n']
            for root_include in root_includes:
                for nested_include in nested_includes:
                    with self.subTest(root_include=root_include, nested_include=nested_include):
                        (directory / "c_gen.c").write_text('/*\n#include <not-code.c>\n*/\n' + root_include, encoding="utf-8")
                        (directory / "c_vendor_lowering.c").write_text(nested_include, encoding="utf-8")
                        (directory / "c_vendor_private.c").write_text("CToken planted;\n", encoding="utf-8")
                        self.assertEqual(3, len(lowering_sources(root)))
                        checked = subprocess.run([sys.executable, str(Path(__file__).with_name("check_c_lowering_provenance.py")), "--root", str(root)],
                                                 capture_output=True, text=True, check=False)
                        self.assertEqual(1, checked.returncode)
                        self.assertIn("c_vendor_private.c:1", checked.stdout)
                        self.assertIn("files=3 violations=1", checked.stdout)
                        (directory / "c_vendor_private.c").write_text("CSourceAnchor source;\n", encoding="utf-8")
                        checked = subprocess.run([sys.executable, str(Path(__file__).with_name("check_c_lowering_provenance.py")), "--root", str(root)],
                                                 capture_output=True, text=True, check=False)
                        self.assertEqual(0, checked.returncode)

    def test_incomplete_include_discovery_fails_the_command(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            directory = root / "src/buster/lib/compiler/frontend/c"
            directory.mkdir(parents=True)
            cases = [('#include "missing.c"\n', "missing.c"),
                     ('#include <buster/lib/compiler/frontend/c/missing.c>\n', "missing.c"),
                     ('#include "../outside.c"\n', "escapes its frontend boundary"),
                     ('#include <buster/lib/compiler/frontend/outside.c>\n', "escapes its frontend boundary"),
                     ('#include LOWERING_IMPLEMENTATION\n', "require a literal quoted or angle path"),
                     ('#include_next <buster/lib/compiler/frontend/c/missing.c>\n', "require a literal quoted or angle path")]
            for source, diagnostic in cases:
                with self.subTest(source=source):
                    (directory / "c_gen.c").write_text(source, encoding="utf-8")
                    checked = subprocess.run([sys.executable, str(Path(__file__).with_name("check_c_lowering_provenance.py")), "--root", str(root)],
                                             capture_output=True, text=True, check=False)
                    self.assertEqual(1, checked.returncode)
                    self.assertIn("incomplete scan", checked.stdout)
                    self.assertIn(diagnostic, checked.stdout)

    def test_required_ci_runs_the_guard_and_planted_read_suite(self):
        source = Path(__file__).resolve().parents[1].joinpath(".github/workflows/ci.yml").read_text(encoding="utf-8")
        commands = "python3 -B tools/check_c_lowering_provenance_test.py -v\n          python3 -B tools/check_c_lowering_provenance.py"
        self.assertEqual(2, source.count(commands))


if __name__ == "__main__":
    unittest.main()
