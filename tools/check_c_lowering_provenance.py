#!/usr/bin/env python3
"""Keep token grammar and source-range node recovery out of C lowering."""

from __future__ import annotations

import argparse
from bisect import bisect_right
from dataclasses import dataclass
from pathlib import Path
import re


LEXEMES = re.compile(
    r'//[^\n]*|/\*[\s\S]*?\*/|"(?:\\[\s\S]|[^"\\])*"|'
    r"'(?:\\[\s\S]|[^'\\])*'|[A-Za-z_][A-Za-z_0-9]*|->|[^\s]"
)
IDENTIFIER = re.compile(r"[A-Za-z_][A-Za-z_0-9]*\Z")
FORBIDDEN_TYPES = {"CToken", "CTokenKind", "CTokenShape", "CPunctuator", "CTokenPositionIndex",
                   "CTypeParseMachine", "CTypeParseFrame", "CTypeMutation"}
FORBIDDEN_RECOVERY = {
    "c_ast_facts_locate", "c_ast_spans_find", "c_ast_spans_locate",
    "c_ir_tree_range_node", "c_ir_type_name_node_at", "c_ir_group_type_name",
    "c_ir_compound_literal_type", "c_ir_sizeof_operand_type_attempt",
    "c_ir_predict_expression_type", "c_ir_conditional_node",
    "c_ast_build", "c_ast_spans_build", "c_ir_core_chain_unit", "chain_first",
    "c_ir_body_nodes_size", "c_ir_body_nodes_build", "c_ir_body_nodes_find", "CIrBodyNodes",
}
# Audited in c_parse.c: these read target layout or finished model tables,
# never source tokens. Scope indices use completed scope intervals; they do
# not reconstruct nodes. New c_parse entries require the same audit.
MODEL_READS = {
    "c_parse_atomic_drops_type_alignment",
    "c_parse_builtin_layout",
    "c_parse_builtin_type_layout",
    "c_parse_name_hash",
    "c_parse_scope_distance",
    "c_parse_scope_for_token",
    "c_parse_scope_for_token_near",
    "c_parse_type_alignment",
    "c_parse_type_is_noreturn",
    "c_parse_aggregate_attributes",
    "c_parse_declaration_binding_fact",
    "c_parse_lookup_existing_entity",
}
# These build local lookup indices using finished declaration/scope rows.
# They change only a lowering-owned model copy, never syntax or model rows.
LOCAL_INDEX_BUILDS = {"c_parse_index_declarations", "c_parse_index_scope_children"}
# Overflow-checked scalar arithmetic, with no model or preprocessing input.
SCALAR_READS = {"c_type_parse_buffer_size_add"}
# Existing semantic descriptor helpers consume completed type/scalar values,
# target policy or literal/assembly spelling. The two source-location entries
# return provenance only. None accepts an expression/type token range.
SEMANTIC_READS = {
    "c_semantic_absolute_value_kind", "c_semantic_asm_bound_register",
    "c_semantic_asm_clobber_matches_constraint", "c_semantic_asm_clobber_name",
    "c_semantic_asm_clobber_valid", "c_semantic_asm_decimal_reference",
    "c_semantic_asm_fixed_operands_conflict", "c_semantic_asm_register_alternative",
    "c_semantic_asm_special_operands_message", "c_semantic_asm_type_class",
    "c_semantic_asm_vector_operand", "c_semantic_asm_x87_operand",
    "c_semantic_asm_x87_operands_message", "c_semantic_atomic_builtin_arity",
    "c_semantic_atomic_builtin_result", "c_semantic_builtin_returns_void",
    "c_semantic_byte_swap_kind", "c_semantic_call_accepts_arity",
    "c_semantic_call_arity_message", "c_semantic_integer_arithmetic_kind",
    "c_semantic_integer_builtin_fold", "c_semantic_integer_builtin_fold_kind",
    "c_semantic_integer_count_parameter_kind", "c_semantic_integer_kind",
    "c_semantic_integer_literal_fits", "c_semantic_integer_literal_kind",
    "c_semantic_integer_literal_limit", "c_semantic_integer_rank",
    "c_semantic_integer_transform_builtin", "c_semantic_math_libm_shape",
    "c_semantic_math_link_is_long_double", "c_semantic_math_long_double_builtin_supported_on_target",
    "c_semantic_math_special_value", "c_semantic_memory_builtin_arity",
    "c_semantic_memory_builtin_result", "c_semantic_overflow_builtin_generic",
    "c_semantic_simd_builtin", "c_semantic_source_file", "c_semantic_source_line",
    "c_semantic_source_location_builtin", "c_semantic_sse2_immediate_shift_builtin",
    "c_semantic_vendor_builtin_signature", "c_semantic_vendor_builtin_supported",
    "c_semantic_vendor_immediate_limit",
}
FORBIDDEN_RANGE_GRAMMAR = {
    "c_ir_count_string_literal_range_for_target",
    "c_ir_decode_string_literal_range_for_target",
    "c_ir_decode_character_value",
    "c_ir_ext80_fold_constant_range",
    "c_ir_primitive_type_kind",
    "c_ir_tokens_are_string_literals",
}
FORBIDDEN_SEMANTIC_GRAMMAR = {
    "c_semantic_alignof_member",
    "c_semantic_check_named_call_arities",
    "c_semantic_ext80_fold_initializer",
    "c_semantic_type_identity_query",
    "c_semantic_storage_half_operand_layout",
    "c_semantic_vendor_selector",
    "c_semantic_vendor_storage_half_argument",
}


@dataclass(frozen=True)
class Token:
    spelling: str
    line: int


def tokens(source: str) -> list[Token]:
    source, splices = splice_lines(source)
    include_lines = {source[:match.start()].count("\n") + 1
                     for match in re.finditer(r"^[ \t]*#[ \t]*include\b[^\n]*", source, re.MULTILINE)}
    result = []
    line = 1
    previous = 0
    for match in LEXEMES.finditer(source):
        line += source[previous:match.start()].count("\n")
        word = match.group()
        if line not in include_lines and not word.startswith(("//", "/*", '"', "'")):
            result.append(Token(word, line + bisect_right(splices, match.start())))
        line += word.count("\n")
        previous = match.end()
    return result


def splice_lines(source: str) -> tuple[str, list[int]]:
    pieces = []
    positions = []
    previous = 0
    removed = 0
    for match in re.finditer(r"\\\r?\n", source):
        pieces.append(source[previous:match.start()])
        positions.append(match.start() - removed)
        removed += match.end() - match.start()
        previous = match.end()
    pieces.append(source[previous:])
    return "".join(pieces), positions


def include_names(source: str) -> list[tuple[str, bool]]:
    source, _ = splice_lines(source)
    pieces = []
    previous = 0
    for match in LEXEMES.finditer(source):
        word = match.group()
        if word.startswith(("//", "/*")):
            pieces.append(source[previous:match.start()])
            pieces.append(" " + "\n" * word.count("\n"))
            previous = match.end()
    pieces.append(source[previous:])
    result = []
    for match in re.finditer(r"^[ \t]*#[ \t]*(include(?:_next)?)\b([^\n]*)", "".join(pieces), re.MULTILINE):
        operand = match.group(2).strip()
        included = re.fullmatch(r'"([^"\n]+)"|<([^>\n]+)>', operand)
        if match.group(1) != "include" or not included:
            raise ValueError(f"lowering includes require a literal quoted or angle path: #{match.group(1)} {operand}")
        name = included.group(1) or included.group(2)
        if name.endswith(".c"):
            result.append((name, included.group(2) is not None))
    return result


def violations(source: str) -> list[Token]:
    lexed = tokens(source)
    aliases = {"preprocess"}
    ast_aliases = {"syntax_tree"}
    qualifiers = {"const", "volatile", "restrict", "*", "("}
    for index, token in enumerate(lexed):
        if token.spelling in {"CPreprocessResult", "CAst"}:
            cursor = index + 1
            while cursor < len(lexed) and lexed[cursor].spelling in qualifiers:
                cursor += 1
            if cursor < len(lexed) and IDENTIFIER.fullmatch(lexed[cursor].spelling):
                (aliases if token.spelling == "CPreprocessResult" else ast_aliases).add(lexed[cursor].spelling)
    # Follow value/pointer copies too, including C23 inferred declarations.
    changed = True
    while changed:
        changed = False
        for index in range(1, len(lexed) - 1):
            if lexed[index].spelling == "=" and IDENTIFIER.fullmatch(lexed[index - 1].spelling):
                cursor = index + 1
                rhs = []
                while cursor < len(lexed) and lexed[cursor].spelling not in {";", ",", "{"}:
                    rhs.append(lexed[cursor].spelling)
                    cursor += 1
                # An API that reads preprocessing provenance does not return
                # the preprocessing object. Propagate direct value/pointer
                # copies only, rather than tainting unrelated return values
                # and then every same-named local in the translation unit.
                copied = [word for word in rhs if word not in {"(", ")", "&", "*"}]
                copy = len(copied) == 1 and copied[0] in aliases
                if copy and lexed[index - 1].spelling not in aliases:
                    aliases.add(lexed[index - 1].spelling)
                    changed = True
    result = []
    for index, token in enumerate(lexed):
        word = token.spelling
        forbidden = (
            word in FORBIDDEN_TYPES or word in FORBIDDEN_RECOVERY or word in FORBIDDEN_RANGE_GRAMMAR
            or word in FORBIDDEN_SEMANTIC_GRAMMAR
            or word.startswith(("C_TOKEN_", "C_PUNCTUATOR_", "c_token_", "c_punctuator_", "c_preprocess_token_"))
            or word.startswith("c_parse_") and word not in MODEL_READS and word not in LOCAL_INDEX_BUILDS
            or word.startswith(("c_parser_", "c_type_parse_")) and word not in SCALAR_READS
            or word.startswith("c_alignment_")
            or word.startswith("c_semantic_") and word not in SEMANTIC_READS
        )
        if word in {"tokens", "token_shapes"} and index and lexed[index - 1].spelling in {".", "->"}:
            cursor = index - 2
            owner = []
            depth = 0
            while cursor >= 0:
                previous = lexed[cursor].spelling
                if previous in {")", "]"}:
                    depth += 1
                elif previous in {"(", "["} and depth:
                    depth -= 1
                elif not depth and previous in {";", "{", "}", "=", ","}:
                    break
                owner.append(previous)
                cursor -= 1
            # Only owned AST anchor columns are permitted. This also rejects
            # raw streams reached through an untyped alias, comma declarator
            # or a preprocessing-result field whose name is not `preprocess`.
            # An AST mentioned earlier in a receiver expression does not
            # authorize another object's stream, such as ast->input.tokens.
            # Lowering projects the anchor column directly from a typed AST
            # alias (or its syntax_tree field), so the immediate receiver
            # must be that alias. More elaborate expressions first bind an
            # explicit CAst pointer, keeping the boundary reviewable.
            forbidden |= not owner or owner[0] in aliases or owner[0] not in ast_aliases
        if forbidden:
            result.append(token)
    return result


def lowering_sources(root: Path) -> list[Path]:
    directory = (root / "src/buster/lib/compiler/frontend/c").resolve()
    # The semantic vendor metadata module is compiled independently. The
    # lowering boundary includes every implementation c_gen incorporates,
    # following nested includes too, rather than guessing from filename stems.
    result = []
    pending = [directory / "c_gen.c"]
    seen = set()
    while pending:
        path = pending.pop()
        if path not in seen:
            seen.add(path)
            result.append(path)
            source = path.read_text(encoding="utf-8")
            for name, src_relative in include_names(source):
                # Repository angle includes use src/, quoted ones the owning file.
                included = ((root / "src" if src_relative else path.parent) / name).resolve()
                if not included.is_relative_to(directory):
                    raise ValueError(f"lowering include escapes its frontend boundary: {included}")
                pending.append(included)
    return sorted(result)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    args = parser.parse_args()
    failures = 0
    sources = []
    try:
        sources = lowering_sources(args.root)
        for path in sources:
            found = violations(path.read_text(encoding="utf-8"))
            failures += len(found)
            for token in found[:20]:
                print(f"{path}:{token.line}: token structure is forbidden in lowering: {token.spelling}")
            if len(found) > 20:
                print(f"{path}: {len(found) - 20} additional violations")
    except (OSError, ValueError) as error:
        failures += 1
        print(f"C_LOWERING_PROVENANCE incomplete scan: {error}")
    print(f"C_LOWERING_PROVENANCE files={len(sources)} violations={failures}")
    return int(failures != 0)


if __name__ == "__main__":
    raise SystemExit(main())
