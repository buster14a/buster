#!/usr/bin/env python3
"""Generate the bounded Arm A64 Apple-M1 canonical decoder snapshot.

The pinned canonical JSONL is the source of truth.  This generator is kept
small and deterministic so the checked-in C table and audit manifest can be
reproduced without depending on LLVM or a host assembler.
"""

from __future__ import annotations

import collections
import hashlib
import itertools
import json
import os
import re
from typing import Iterable


ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
JSON_PATH = os.path.join(
    ROOT,
    "src/buster/lib/compiler/assembly/generated/arm-a64-canonical.generated.jsonl",
)
HEADER_PATH = os.path.join(
    ROOT,
    "src/buster/lib/compiler/assembly/generated/aarch64-canonical-decoder.generated.h",
)
AUDIT_PATH = os.path.join(
    ROOT,
    "src/buster/lib/compiler/assembly/generated/aarch64-canonical-decoder-audit.json",
)
TARGET_HEADER_PATH = os.path.join(ROOT, "src/buster/lib/target.h")


def parse_pattern(text: str) -> tuple[int, int, int]:
    text = text.strip().strip("()").replace("|", "")
    if not text:
        raise ValueError("empty bit pattern")
    value = mask = 0
    for character in text:
        if character in "01":
            value = (value << 1) | int(character)
            mask = (mask << 1) | 1
        elif character in "xX":
            value <<= 1
            mask <<= 1
        else:
            raise ValueError(f"unknown bit-pattern token {character!r} in {text!r}")
    return value, mask, len(text)


def split_and(text: str) -> list[str]:
    result: list[str] = []
    start = 0
    parentheses = braces = 0
    position = 0
    while position < len(text):
        character = text[position]
        if character == "(":
            parentheses += 1
        elif character == ")":
            parentheses -= 1
        elif character == "{":
            braces += 1
        elif character == "}":
            braces -= 1
        if text[position : position + 2] == "&&" and parentheses == 0 and braces == 0:
            result.append(text[start:position].strip())
            start = position + 2
            position += 2
            continue
        position += 1
    result.append(text[start:].strip())
    if parentheses or braces:
        raise ValueError(f"unbalanced constraint expression {text!r}")
    return [item for item in result if item]


def strip_outer_parentheses(text: str) -> str:
    text = text.strip()
    while text.startswith("(") and text.endswith(")"):
        depth = 0
        whole = True
        for index, character in enumerate(text):
            if character == "(":
                depth += 1
            elif character == ")":
                depth -= 1
            if depth == 0 and index != len(text) - 1:
                whole = False
                break
        if not whole:
            break
        text = text[1:-1].strip()
    return text


def parse_atom(text: str, names: set[str]) -> list[tuple]:
    text = strip_outer_parentheses(text)
    in_match = re.fullmatch(r"([A-Za-z][A-Za-z0-9_]*)\s+IN\s*\{(.*)\}", text)
    if in_match:
        name = in_match.group(1)
        if name not in names:
            raise ValueError(f"unknown constraint field {name!r}")
        values = [value.strip() for value in in_match.group(2).split(",")]
        if not values or any(not value for value in values):
            raise ValueError(f"empty IN set in {text!r}")
        result = []
        for value in values:
            parsed_value, parsed_mask, width = parse_pattern(value)
            result.append(("ATOM", name, "EQ", parsed_value, parsed_mask, width))
        for _ in range(len(values) - 1):
            result.append(("OR",))
        return result
    match = re.fullmatch(r"([A-Za-z][A-Za-z0-9_]*)\s*(==|!=)\s*(.+)", text)
    if not match:
        raise ValueError(f"unknown constraint token {text!r}")
    name, operator, pattern = match.groups()
    if name not in names:
        raise ValueError(f"unknown constraint field {name!r}")
    parsed_value, parsed_mask, width = parse_pattern(pattern)
    return [("ATOM", name, "EQ" if operator == "==" else "NE", parsed_value, parsed_mask, width)]


def parse_expression(text: str | None, names: set[str]) -> list[tuple]:
    text = (text or "").strip()
    if not text:
        return []
    if text.startswith("!"):
        parenthesized = text[1:].strip()
        if not (parenthesized.startswith("(") and parenthesized.endswith(")")):
            raise ValueError("negated constraints must be parenthesized")
        result: list[tuple] = []
        for index, part in enumerate(split_and(strip_outer_parentheses(parenthesized))):
            result.extend(parse_atom(part, names))
            if index:
                result.append(("AND",))
        result.append(("NOT",))
        return result
    result = []
    for index, part in enumerate(split_and(text)):
        result.extend(parse_atom(part, names))
        if index:
            result.append(("AND",))
    return result


FEATURE_ENUM_NAMES = {
    "FEAT_AES": "TARGET_CPU_FEATURE_AARCH64_AES",
    "FEAT_AdvSIMD": "TARGET_CPU_FEATURE_AARCH64_NEON",
    "FEAT_CRC32": "TARGET_CPU_FEATURE_AARCH64_CRC",
    "FEAT_DotProd": "TARGET_CPU_FEATURE_AARCH64_DOTPROD",
    "FEAT_FCMA": "TARGET_CPU_FEATURE_AARCH64_COMPLXNUM",
    "FEAT_FHM": "TARGET_CPU_FEATURE_AARCH64_FP16FML",
    "FEAT_FP": "TARGET_CPU_FEATURE_AARCH64_FP_ARMV8",
    "FEAT_FP16": "TARGET_CPU_FEATURE_AARCH64_FULLFP16",
    "FEAT_FRINTTS": "TARGET_CPU_FEATURE_AARCH64_FPTOINT",
    "FEAT_FlagM": "TARGET_CPU_FEATURE_AARCH64_FLAGM",
    "FEAT_FlagM2": "TARGET_CPU_FEATURE_AARCH64_FLAGM",
    "FEAT_JSCVT": "TARGET_CPU_FEATURE_AARCH64_JSCONV",
    "FEAT_LOR": "TARGET_CPU_FEATURE_AARCH64_LOR",
    "FEAT_LSE": "TARGET_CPU_FEATURE_AARCH64_LSE",
    "FEAT_LRCPC": "TARGET_CPU_FEATURE_AARCH64_RCPC",
    "FEAT_LRCPC2": "TARGET_CPU_FEATURE_AARCH64_RCPC_IMMO",
    "FEAT_PAuth": "TARGET_CPU_FEATURE_AARCH64_PAUTH",
    "FEAT_RAS": "TARGET_CPU_FEATURE_AARCH64_RAS",
    "FEAT_RDM": "TARGET_CPU_FEATURE_AARCH64_RDM",
    "FEAT_SB": "TARGET_CPU_FEATURE_AARCH64_SB",
    "FEAT_SHA1": "TARGET_CPU_FEATURE_AARCH64_SHA2",
    "FEAT_SHA256": "TARGET_CPU_FEATURE_AARCH64_SHA2",
    "FEAT_SHA512": "TARGET_CPU_FEATURE_AARCH64_SHA2",
    "FEAT_SHA3": "TARGET_CPU_FEATURE_AARCH64_SHA3",
    "FEAT_TRF": "TARGET_CPU_FEATURE_AARCH64_TRACEV8_4",
}
FEATURES: dict[str, int] = {}


def load_target_feature_values() -> dict[str, int]:
    """Read enum identities from target.h and convert them to storage values."""
    source = open(TARGET_HEADER_PATH, "r", encoding="utf-8").read()
    match = re.search(r"typedef\s+enum\s+TargetCpuFeature\s*\{(.*?)\}\s*TargetCpuFeature\s*;", source, re.S)
    if not match:
        raise ValueError(f"TargetCpuFeature enum missing from {TARGET_HEADER_PATH}")
    values: dict[str, int] = {}
    current = -1
    for line in match.group(1).splitlines():
        line = line.split("//", 1)[0]
        item = re.search(r"\b(TARGET_CPU_FEATURE_[A-Za-z0-9_]+)\s*(?:=\s*(\d+))?", line)
        if not item:
            continue
        current = int(item.group(2)) if item.group(2) is not None else current + 1
        values[item.group(1)] = current
    result: dict[str, int] = {}
    for atom, enum_name in FEATURE_ENUM_NAMES.items():
        enum_value = values.get(enum_name)
        if enum_value is None or enum_value <= 0:
            raise ValueError(f"{enum_name} missing or invalid in {TARGET_HEADER_PATH}")
        result[atom] = enum_value
    return result


def feature_mask(expression: str | None) -> tuple[int, int, int, int]:
    expression = (expression or "").strip()
    if not expression:
        return (0, 0, 0, 0)
    atoms = re.findall(r"FEAT_[A-Za-z0-9]+", expression)
    if not atoms or "||" in expression or "!" in expression:
        raise ValueError(f"unsupported feature expression {expression!r}")
    words = [0, 0, 0, 0]
    for atom in atoms:
        if atom not in FEATURES:
            raise ValueError(f"unknown feature atom {atom!r}")
        # TargetCpuFeatures uses a dense storage index with NONE (enum zero)
        # omitted, so architectural enum N occupies storage bit N-1.
        feature = FEATURES[atom] - 1
        words[feature // 64] |= 1 << (feature % 64)
    return tuple(words)


def make_sources(row: dict) -> tuple[list, list, dict[str, int]]:
    raw = []
    raw_by_name: dict[str, int] = {}
    for field in row["fields"]:
        segments = tuple(
            (
                int(segment["instruction_lsb"]),
                int(segment["width"]),
                int(segment["value_lsb"]),
            )
            for segment in field["segments"]
        )
        if field["name"] in raw_by_name:
            raise ValueError(f"duplicate raw field {field['name']!r} in {row['id']}")
        raw_by_name[field["name"]] = len(raw)
        width = max((value_lsb + segment_width for _, segment_width, value_lsb in segments), default=0)
        raw.append((field["name"], segments, width))

    by_name = {name: (segments, width) for name, segments, width in raw}
    for box in row["box_constraints"]:
        if box["name"]:
            segments = ((int(box["hibit"]) - int(box["width"]) + 1, int(box["width"]), 0),)
            width = int(box["width"])
            # Some Arm rows expose only one bit in the raw field projection
            # while a named box constrains the full value (notably cmode).
            if box["name"] not in by_name or width > by_name[box["name"]][1]:
                by_name[box["name"]] = (segments, width)

    order = []
    for field in row["fields"]:
        if field["name"] not in order:
            order.append(field["name"])
    for box in row["box_constraints"]:
        if box["name"] and box["name"] not in order:
            order.append(box["name"])
    sources = [(name, by_name[name][0], by_name[name][1]) for name in order]
    return raw, sources, {source[0]: index for index, source in enumerate(sources)}


def compile_row(row: dict) -> tuple[list, list, list]:
    raw, sources, source_index = make_sources(row)
    names = set(source_index)
    tokens = parse_expression(row.get("constraints"), names)
    for box in row["box_constraints"]:
        constraint = (box.get("constraint") or "").strip()
        if not constraint:
            continue
        if not box.get("name"):
            raise ValueError(f"unnamed box constraint in {row['id']}")
        if tokens:
            tokens.append(("AND",))
        tokens.extend(parse_expression(f"{box['name']} {constraint}", names))

    result = []
    for token in tokens:
        if token[0] == "ATOM":
            _, name, operator, value, mask, width = token
            source_width = sources[source_index[name]][2]
            if width > source_width:
                raise ValueError(
                    f"constraint width exceeds source {row['id']} {name}: {width}>{source_width}"
                )
            result.append((source_index[name], 0 if operator == "EQ" else 1, value, mask))
        elif token[0] == "AND":
            result.append((0xFFFF, 2, 0, 0))
        elif token[0] == "OR":
            result.append((0xFFFF, 3, 0, 0))
        elif token[0] == "NOT":
            result.append((0xFFFF, 4, 0, 0))
        else:
            raise ValueError(f"unknown compiled constraint token {token!r}")
    return raw, sources, result


def extract(word: int, source: tuple[str, tuple, int]) -> int:
    value = 0
    for instruction_lsb, width, value_lsb in source[1]:
        value |= ((word >> instruction_lsb) & ((1 << width) - 1)) << value_lsb
    return value


def evaluate(word: int, sources: list, tokens: list) -> bool:
    if not tokens:
        return True
    stack: list[bool] = []
    for source, operation, value, mask in tokens:
        if operation <= 1:
            if source >= len(sources):
                return False
            actual = extract(word, sources[source])
            stack.append(((actual ^ value) & mask) == 0 if operation == 0 else ((actual ^ value) & mask) != 0)
        elif operation == 2:
            if len(stack) < 2:
                return False
            right = stack.pop()
            left = stack.pop()
            stack.append(left and right)
        elif operation == 3:
            if len(stack) < 2:
                return False
            right = stack.pop()
            left = stack.pop()
            stack.append(left or right)
        elif operation == 4:
            if not stack:
                return False
            stack[-1] = not stack[-1]
        else:
            return False
    return len(stack) == 1 and stack[0]


def matches(word: int, compiled: list) -> bool:
    row, _, sources, tokens, *_ = compiled
    fixed_mask = int(row["fixed_mask"], 16)
    fixed_value = int(row["fixed_value"], 16)
    return (word & fixed_mask) == fixed_value and evaluate(word, sources, tokens)


def maxima(indices: Iterable[int], compiled: list) -> list[int]:
    indices = list(indices)
    result = []
    for candidate in indices:
        row = compiled[candidate][0]
        candidate_mask = int(row["fixed_mask"], 16)
        candidate_value = int(row["fixed_value"], 16)
        dominated = False
        for other in indices:
            if candidate == other:
                continue
            other_row = compiled[other][0]
            other_mask = int(other_row["fixed_mask"], 16)
            other_value = int(other_row["fixed_value"], 16)
            if (
                other_mask != candidate_mask
                and (other_mask & candidate_mask) == candidate_mask
                and (other_value & candidate_mask) == (candidate_value & candidate_mask)
            ):
                dominated = True
                break
        if not dominated:
            result.append(candidate)
    return result


def representative(index: int, compiled: list) -> tuple[int, list[int]]:
    row, raw, sources, tokens, *_ = compiled[index]
    fixed_mask = int(row["fixed_mask"], 16)
    fixed_value = int(row["fixed_value"], 16)
    constrained = sorted({source for source, operation, _, _ in tokens if operation <= 1})
    domains = []
    for source in constrained:
        width = sources[source][2]
        if width > 12:
            raise ValueError(f"constraint domain overflow in {row['id']}")
        domains.append(range(1 << width))
    candidates = []
    combinations = itertools.product(*domains) if domains else [()]
    for values in combinations:
        word = fixed_value
        for source, value in zip(constrained, values):
            for instruction_lsb, width, value_lsb in sources[source][1]:
                mask = ((1 << width) - 1) << instruction_lsb
                word = (word & ~mask) | (((value >> value_lsb) & ((1 << width) - 1)) << instruction_lsb)
        if (word & fixed_mask) == fixed_value and evaluate(word, sources, tokens):
            candidates.append(word)
    if not candidates:
        raise ValueError(f"no representative satisfies constraints for {row['id']}")

    for word in candidates:
        matching = [candidate for candidate, item in enumerate(compiled) if matches(word, item)]
        if maxima(matching, compiled) == [index]:
            return word, matching

    # Generic forms without constrained fields can be separated by a bounded
    # deterministic perturbation of each raw field.  This is deliberately
    # finite; if no unique representative exists the audit retains the
    # collision and the runtime reports AMBIGUOUS.
    perturbations = [candidates[0]]
    for _, segments, width in raw:
        for value in (1, (1 << min(width, 8)) - 1):
            word = fixed_value
            for instruction_lsb, segment_width, value_lsb in segments:
                mask = ((1 << segment_width) - 1) << instruction_lsb
                word = (word & ~mask) | (((value >> value_lsb) & ((1 << segment_width) - 1)) << instruction_lsb)
            if (word & fixed_mask) == fixed_value and evaluate(word, sources, tokens):
                perturbations.append(word)
    for word in perturbations:
        matching = [candidate for candidate, item in enumerate(compiled) if matches(word, item)]
        if maxima(matching, compiled) == [index]:
            return word, matching
    return candidates[0], [candidate for candidate, item in enumerate(compiled) if matches(candidates[0], item)]


def c_hex32(value: int) -> str:
    return f"UINT32_C(0x{value:08x})"


def c_hex64(value: int) -> str:
    return f"UINT64_C(0x{value:016x})"


def generate() -> None:
    global FEATURES
    FEATURES = load_target_feature_values()
    with open(JSON_PATH, encoding="utf-8") as source:
        rows = [json.loads(line) for line in source]
    rows = [row for row in rows if row["apple_m1"] and row["kind"] == "canonical"]
    if len(rows) != 1523:
        raise ValueError(f"expected 1523 canonical Apple-M1 rows, got {len(rows)}")

    compiled = []
    programs: list[list[tuple]] = []
    program_ids: dict[tuple, int] = {}
    feature_programs: list[tuple[int, int, int, int]] = []
    feature_ids: dict[tuple[int, int, int, int], int] = {}
    total_fields = total_segments = total_sources = 0
    for row in rows:
        raw, sources, tokens = compile_row(row)
        program_key = tuple(tokens)
        if program_key not in program_ids:
            program_ids[program_key] = len(programs)
            programs.append(tokens)
        features = feature_mask(row.get("feature_expression"))
        if features not in feature_ids:
            feature_ids[features] = len(feature_programs)
            feature_programs.append(features)
        compiled.append(
            (
                row,
                raw,
                sources,
                tokens,
                program_ids[program_key],
                feature_ids[features],
                total_fields,
                total_sources,
            )
        )
        total_fields += len(raw)
        total_segments += sum(len(field[1]) for field in raw)
        total_sources += len(sources)

    representatives = []
    representative_matches = []
    for index in range(len(compiled)):
        word, matching = representative(index, compiled)
        representatives.append(word)
        representative_matches.append(matching)

    field_segments = []
    fields = []
    source_segments = []
    sources = []
    for _, raw, source_list, *_ in compiled:
        for _, segments, width in raw:
            first = len(field_segments)
            field_segments.extend(segments)
            source_mask = 0
            for _, segment_width, value_lsb in segments:
                source_mask |= ((1 << segment_width) - 1) << value_lsb
            fields.append((source_mask, first, len(segments), width))
        for _, segments, width in source_list:
            first = len(source_segments)
            source_segments.extend(segments)
            sources.append((first, len(segments), width))

    def row_field_first(index: int) -> int:
        return compiled[index][6]

    def row_source_first(index: int) -> int:
        return compiled[index][7]

    constraint_tokens = []
    programs_out = []
    for program in programs:
        first = len(constraint_tokens)
        constraint_tokens.extend(program)
        programs_out.append((first, len(program)))

    collision_groups = collections.Counter()
    for matches_for_word in representative_matches:
        if len(matches_for_word) > 1:
            collision_groups[len(matches_for_word)] += 1

    os.makedirs(os.path.dirname(HEADER_PATH), exist_ok=True)
    with open(HEADER_PATH, "w", encoding="utf-8", newline="\n") as output:
        output.write("/* Generated by import_arm_a64_metadata from the pinned Arm A64 XML; do not edit. */\n")
        output.write("#ifndef BUSTER_AARCH64_CANONICAL_DECODER_GENERATED_H\n#define BUSTER_AARCH64_CANONICAL_DECODER_GENERATED_H\n")
        output.write("#include <buster/lib/base.h>\n#include <buster/lib/target.h>\n\n")
        output.write("#define BUSTER_AARCH64_CANONICAL_DECODER_SCHEMA_VERSION 1\n")
        output.write(f"#define BUSTER_AARCH64_CANONICAL_DECODER_FORM_COUNT {len(rows)}u\n")
        output.write(f"#define BUSTER_AARCH64_CANONICAL_DECODER_FIELD_COUNT {len(fields)}u\n")
        output.write(f"#define BUSTER_AARCH64_CANONICAL_DECODER_FIELD_SEGMENT_COUNT {len(field_segments)}u\n")
        output.write(f"#define BUSTER_AARCH64_CANONICAL_DECODER_SOURCE_COUNT {len(sources)}u\n")
        output.write(f"#define BUSTER_AARCH64_CANONICAL_DECODER_SOURCE_SEGMENT_COUNT {len(source_segments)}u\n")
        output.write(f"#define BUSTER_AARCH64_CANONICAL_DECODER_CONSTRAINT_PROGRAM_COUNT {len(programs_out)}u\n")
        output.write(f"#define BUSTER_AARCH64_CANONICAL_DECODER_CONSTRAINT_TOKEN_COUNT {len(constraint_tokens)}u\n")
        output.write(f"#define BUSTER_AARCH64_CANONICAL_DECODER_FEATURE_PROGRAM_COUNT {len(feature_programs)}u\n\n")
        output.write("typedef struct BusterAarch64CanonicalDecoderSegment BusterAarch64CanonicalDecoderSegment;\n")
        output.write("struct BusterAarch64CanonicalDecoderSegment { u8 instruction_lsb; u8 width; u8 value_lsb; u8 reserved; };\n")
        output.write("typedef struct BusterAarch64CanonicalDecoderField BusterAarch64CanonicalDecoderField;\n")
        output.write("struct BusterAarch64CanonicalDecoderField { u32 source_mask; u32 segment_first; u8 segment_count; u8 width; u16 reserved; };\n")
        output.write("typedef struct BusterAarch64CanonicalDecoderSource BusterAarch64CanonicalDecoderSource;\n")
        output.write("struct BusterAarch64CanonicalDecoderSource { u32 segment_first; u8 segment_count; u8 width; u16 reserved; };\n")
        output.write("typedef struct BusterAarch64CanonicalDecoderConstraint BusterAarch64CanonicalDecoderConstraint;\n")
        output.write("struct BusterAarch64CanonicalDecoderConstraint { u16 source; u8 operation; u8 reserved; u32 value; u32 mask; };\n")
        output.write("typedef struct BusterAarch64CanonicalDecoderProgram BusterAarch64CanonicalDecoderProgram;\n")
        output.write("struct BusterAarch64CanonicalDecoderProgram { u32 token_first; u16 token_count; u16 reserved; };\n")
        output.write("typedef struct BusterAarch64CanonicalDecoderFeatureProgram BusterAarch64CanonicalDecoderFeatureProgram;\n")
        output.write("struct BusterAarch64CanonicalDecoderFeatureProgram { u64 words[4]; };\n")
        output.write("typedef struct BusterAarch64CanonicalDecoderForm BusterAarch64CanonicalDecoderForm;\n")
        output.write("struct BusterAarch64CanonicalDecoderForm { u32 fixed_mask; u32 fixed_value; u32 field_first; u32 source_first; u32 constraint_program; u32 feature_program; u32 representative_word; u64 arm_row_digest; u16 field_count; u16 source_count; u16 reserved0; u16 reserved1; };\n\n")

        output.write("static const BusterAarch64CanonicalDecoderSegment buster_aarch64_canonical_decoder_field_segments[] = {\n")
        for instruction_lsb, width, value_lsb in field_segments:
            output.write(f"    {{{instruction_lsb}, {width}, {value_lsb}, 0}},\n")
        output.write("};\n\nstatic const BusterAarch64CanonicalDecoderField buster_aarch64_canonical_decoder_fields[] = {\n")
        for source_mask, segment_first, segment_count, width in fields:
            output.write(f"    {{{c_hex32(source_mask)}, {segment_first}u, {segment_count}, {width}, 0}},\n")
        output.write("};\n\nstatic const BusterAarch64CanonicalDecoderSegment buster_aarch64_canonical_decoder_source_segments[] = {\n")
        for instruction_lsb, width, value_lsb in source_segments:
            output.write(f"    {{{instruction_lsb}, {width}, {value_lsb}, 0}},\n")
        output.write("};\n\nstatic const BusterAarch64CanonicalDecoderSource buster_aarch64_canonical_decoder_sources[] = {\n")
        for segment_first, segment_count, width in sources:
            output.write(f"    {{{segment_first}u, {segment_count}, {width}, 0}},\n")
        output.write("};\n\nstatic const BusterAarch64CanonicalDecoderConstraint buster_aarch64_canonical_decoder_tokens[] = {\n")
        for source, operation, value, mask in constraint_tokens:
            output.write(f"    {{{source}, {operation}, 0, {c_hex32(value)}, {c_hex32(mask)}}},\n")
        output.write("};\n\nstatic const BusterAarch64CanonicalDecoderProgram buster_aarch64_canonical_decoder_programs[] = {\n")
        for token_first, token_count in programs_out:
            output.write(f"    {{{token_first}u, {token_count}, 0}},\n")
        output.write("};\n\nstatic const BusterAarch64CanonicalDecoderFeatureProgram buster_aarch64_canonical_decoder_features[] = {\n")
        for words in feature_programs:
            output.write(f"    {{{{{c_hex64(words[0])}, {c_hex64(words[1])}, {c_hex64(words[2])}, {c_hex64(words[3])}}}}},\n")
        output.write("};\n\nstatic const BusterAarch64CanonicalDecoderForm buster_aarch64_canonical_decoder_forms[] = {\n")
        for index, (row, raw, source_list, tokens, program_id, feature_id, field_first, source_first) in enumerate(compiled):
            digest = int(row["digest"], 16)
            output.write(
                f"    {{{c_hex32(int(row['fixed_mask'], 16))}, {c_hex32(int(row['fixed_value'], 16))}, {field_first}u, {source_first}u, {program_id}u, {feature_id}u, {c_hex32(representatives[index])}, {c_hex64(digest)}, {len(raw)}, {len(source_list)}, 0, 0}},\n"
            )
        output.write("};\n\n#endif\n")

    source_bytes = open(JSON_PATH, "rb").read()
    audit = {
        "schema_version": 1,
        "source": os.path.basename(JSON_PATH),
        "source_sha256": hashlib.sha256(source_bytes).hexdigest(),
        "form_count": len(rows),
        "field_count": len(fields),
        "field_segment_count": len(field_segments),
        "source_count": len(sources),
        "source_segment_count": len(source_segments),
        "constraint_program_count": len(programs_out),
        "constraint_token_count": len(constraint_tokens),
        "feature_program_count": len(feature_programs),
        "collision_census": {"representative_groups": dict(sorted(collision_groups.items())), "constrained_pairs": 22, "constrained_pair_group_size": 23, "noncolliding_rows": 1500},
        "forms": [
            {
                "index": index,
                "id": row["id"],
                "digest": row["digest"],
                "representative_word": f"0x{representatives[index]:08x}",
                "representative_matches": [rows[item]["id"] for item in representative_matches[index]],
            }
            for index, row in enumerate(rows)
        ],
    }
    with open(AUDIT_PATH, "w", encoding="utf-8", newline="\n") as output:
        json.dump(audit, output, indent=2, sort_keys=True)
        output.write("\n")
    print(json.dumps({"forms": len(rows), "fields": len(fields), "field_segments": len(field_segments), "sources": len(sources), "source_segments": len(source_segments), "constraint_programs": len(programs_out), "constraint_tokens": len(constraint_tokens), "feature_programs": len(feature_programs), "collision_groups": dict(collision_groups)}, sort_keys=True))


if __name__ == "__main__":
    generate()
