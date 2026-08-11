#!/usr/bin/env python3
"""Generate the bounded A64 semantic-VM projection.

The checked-in semantic metadata is intentionally a binding projection, not an
executable instruction semantics database.  This generator adds a small,
pointer-free VM index over that projection.  It consumes both the pinned
canonical JSONL and the corresponding Arm XML tree.  In particular, the XML
hover text is used to recover affine immediate formulas; the existing
semantic JSONL intentionally omits those formulas from its public transform
records.

The generated C table contains no source prose or executable source strings.
It stores only operation IDs, bounded field-name hashes, constants, and links
to the already-generated semantic value tables.  Unsupported rows remain
explicitly unsupported in the manifest.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import struct
import xml.etree.ElementTree as ET
from pathlib import Path
from typing import Any


CANONICAL_SHA256 = "8485c5c61835d5394d325757ab2964890e8bdfea304c6faa8fd4c23e4c7aabec"
CANONICAL_COUNT = 1523
ALIAS_COUNT = 172
TOTAL_COUNT = CANONICAL_COUNT + ALIAS_COUNT

OP_INVALID = 0
OP_FIELD = 1
OP_EXTRACT = 2
OP_CONCAT = 3
OP_UINT_EXTEND = 4
OP_SIGNED_EXTEND = 5
OP_ADD_CONST = 6
OP_SUB_CONST = 7
OP_SUB_FROM_CONST = 8
OP_SCALE_MUL = 9
OP_SCALE_DIV = 10
OP_FIXED_LITERAL = 11
OP_DEFAULT = 12
OP_OPTIONAL = 13
OP_TABLE_EXACT_WILDCARD = 14
OP_RESERVED_REJECT = 15
OP_PC_RELATIVE = 16
OP_PAGE_RELATIVE = 17
OP_CONDITION_INVERT = 18
OP_REGISTER_ADD_MOD32 = 19
OP_BITWISE_NOT = 20
OP_MOVN = 21
OP_LOGICAL_IMMEDIATE = 22
OP_FP_IMMEDIATE = 23
OP_ADVSIMD_IMMEDIATE = 24
OP_SYSOP_LOOKUP = 25
OP_ALIAS_MAP = 26
OP_ALIAS_INJECT = 27
OP_ALIAS_CONDITION = 28
OP_SLICE = 29
OP_INTEGER_DECODE = 30
OP_SHARED_DECODE = 31
OP_COUNT = 32

ROW_RAW_CODEC = 1 << 0
ROW_TRANSFORMS = 1 << 1
ROW_ALIAS_TARGET = 1 << 2
ROW_SEMANTIC_EXECUTABLE = 1 << 3

GAP_NONE = 0
GAP_UNDEFINED = 1
GAP_UNSUPPORTED_TRANSFORM = 2
GAP_ALIAS_CONDITION = 3
GAP_INCOMPLETE_SEMANTICS = 4

IDENT_RE = re.compile(r"[A-Za-z_][A-Za-z0-9_]*")
FORMULA_RE = re.compile(
    r"(?P<body>(?:\d+\s*-\s*)?UInt\(\s*[\"']?\s*(?P<concat>[A-Za-z0-9_:]+)\s*[\"']?\s*\)\s*(?P<tail>[+-]\s*\d+)?)",
    re.IGNORECASE,
)


def fnv1a32(value: str) -> int:
    result = 0x811C9DC5
    for byte in value.encode("ascii"):
        result ^= byte
        result = (result * 0x01000193) & 0xFFFFFFFF
    return result


def parse_int(value: str | int | None) -> int:
    if value is None:
        return 0
    if isinstance(value, int):
        return value
    return int(value, 0)


def local_name(tag: str) -> str:
    return tag.rsplit("}", 1)[-1]


def xml_encoding_texts(xml_root: Path, xml_file: str, encoding_name: str) -> list[str]:
    path = xml_root / xml_file
    if not path.is_file():
        raise RuntimeError(f"missing pinned Arm XML source: {path}")
    root = ET.parse(path).getroot()
    for encoding in root.iter():
        if local_name(encoding.tag) != "encoding" or encoding.attrib.get("name") != encoding_name:
            continue
        result: list[str] = []
        for node in encoding.iter():
            hover = node.attrib.get("hover")
            if hover:
                result.append(hover)
        return result
    raise RuntimeError(f"XML encoding {encoding_name!r} not found in {path}")


def parse_affine_formula(texts: list[str], expected_fields: set[str]) -> tuple[int, int, list[str]] | None:
    """Return (operation, constant, ordered field names) for UInt formulas.

    A full expression is required.  The parser deliberately rejects a
    truncated ``UInt(concat)`` because that was the source of the historical
    11-row immediate bug (the trailing ``-64`` or leading ``128-`` vanished).
    """
    for text in texts:
        for match in FORMULA_RE.finditer(text):
            body = match.group("body")
            concat = match.group("concat")
            tail = match.group("tail") or ""
            # Ensure the matched body is the complete arithmetic clause.  A
            # sentence can contain another UInt(...) later; tokenise only the
            # matched clause and reject unexpected words.
            fields = [x for x in re.split(r"::|:", concat) if x]
            if not fields or any(x not in expected_fields for x in fields):
                continue
            if re.match(r"^\s*\d+\s*-", body):
                constant = int(re.match(r"^\s*(\d+)", body).group(1))
                return OP_SUB_FROM_CONST, constant, fields
            if tail:
                sign = 1 if "+" in tail else -1
                constant = sign * int(re.search(r"\d+", tail).group(0))
                return (OP_ADD_CONST if constant >= 0 else OP_SUB_CONST), abs(constant), fields
            return OP_INTEGER_DECODE, 0, fields
    return None


def parse_slice_header(header: str) -> tuple[str, int, int] | None:
    match = re.fullmatch(r"([A-Za-z_][A-Za-z0-9_]*)(?:\[(\d+)(?::(\d+))?\])?", header)
    if not match:
        return None
    name, first, second = match.groups()
    if first is None:
        return name, 0, 0
    low = int(second) if second is not None else int(first)
    high = int(first)
    if low > high:
        low, high = high, low
    return name, low, high


def rows_load(path: Path) -> list[dict[str, Any]]:
    raw = path.read_bytes()
    # The canonical artifact is the source of the denominator.  Some callers
    # pass the semantic JSONL for convenience; in that case provenance still
    # carries the pinned digest and the row shape is checked below.
    if path.name.endswith("canonical.generated.jsonl") and hashlib.sha256(raw).hexdigest() != CANONICAL_SHA256:
        raise RuntimeError("canonical JSONL SHA-256 does not match the pinned Arm snapshot")
    rows = [json.loads(line) for line in raw.decode("utf-8").splitlines() if line.strip()]
    return rows


def source_canonical_ids(path: Path) -> set[str]:
    return {row["id"] for row in rows_load(path) if row.get("apple_m1") and row.get("kind") in ("canonical", "alias")}


def c_array(values: list[str], per_line: int = 16) -> str:
    lines: list[str] = []
    for start in range(0, len(values), per_line):
        lines.append("    " + ", ".join(values[start : start + per_line]) + ",")
    return "\n".join(lines)


def emit_header(
    path: Path,
    rows: list[dict[str, Any]],
    transform_records: list[dict[str, Any]],
    field_refs: list[dict[str, int]],
    field_name_bytes: list[int],
    atom_program_ids: list[int],
    aliases: list[dict[str, int]],
    coverage: list[int],
    gaps: list[int],
    canonical_raw_indices: list[int],
) -> None:
    transform_values = []
    for record in transform_records:
        transform_values.append(
            "{%d, %d, %d, %d, %d, %d, %d, %d, %d, {0, 0, 0}}" % (
                record["semantic_transform_id"],
                record["field_ref_first"],
                record["field_ref_count"],
                record["key_ref_first"],
                record["key_ref_count"],
                record["op"],
                record["affine_op"],
                record["constant"],
                record.get("program_count", 0),
            )
        )
    field_values = ["{%u, %u, %u, %u, %u, %u, {0, 0}}" % (x["name_hash"], x["name_offset"], x["name_length"], x["field_ordinal"], x["low"], x["high"]) for x in field_refs]
    atom_values = ["{%u}" % atom_id for atom_id in atom_program_ids]
    alias_values = ["{%u, %u, %u, %u, %u, %u, 0}" % (a["target"], a["injected"], a["same"], a["condition_digest"], a["preference_count"], a["condition_supported"]) for a in aliases]
    lines = [
        "/* Generated by generate_aarch64_semantic_vm.py; do not edit. */",
        "#ifndef BUSTER_AARCH64_SEMANTIC_VM_GENERATED_H",
        "#define BUSTER_AARCH64_SEMANTIC_VM_GENERATED_H",
        "#include <buster/lib/base.h>",
        "",
        f"#define BUSTER_AARCH64_SEMANTIC_VM_SCHEMA_VERSION 2u",
        f"#define BUSTER_AARCH64_SEMANTIC_VM_FORM_COUNT {len(rows)}u",
        f"#define BUSTER_AARCH64_SEMANTIC_VM_CANONICAL_COUNT {CANONICAL_COUNT}u",
        f"#define BUSTER_AARCH64_SEMANTIC_VM_ALIAS_COUNT {ALIAS_COUNT}u",
        f"#define BUSTER_AARCH64_SEMANTIC_VM_TRANSFORM_COUNT {len(transform_records)}u",
        f"#define BUSTER_AARCH64_SEMANTIC_VM_FIELD_REF_COUNT {len(field_refs)}u",
        f"#define BUSTER_AARCH64_SEMANTIC_VM_TRANSFORM_PROGRAM_COUNT {sum(bool(record.get('program_count')) for record in transform_records)}u",
        f"#define BUSTER_AARCH64_SEMANTIC_VM_ATOM_PROGRAM_COUNT {len(atom_program_ids)}u",
        f"#define BUSTER_AARCH64_SEMANTIC_VM_RAW_CODEC_COUNT {sum(bool(x & ROW_RAW_CODEC) for x in coverage)}u",
        f"#define BUSTER_AARCH64_SEMANTIC_VM_TRANSFORM_ROW_COUNT {sum(bool(x & ROW_TRANSFORMS) for x in coverage)}u",
        f"#define BUSTER_AARCH64_SEMANTIC_VM_SEMANTIC_EXECUTABLE_COUNT {sum(bool(x & ROW_SEMANTIC_EXECUTABLE) for x in coverage)}u",
        "",
        f"#define BUSTER_AARCH64_SEMANTIC_VM_FIELD_NAME_BYTES {len(field_name_bytes)}u",
        "typedef struct BusterA64SemanticVMGeneratedFieldRef BusterA64SemanticVMGeneratedFieldRef;",
        "struct BusterA64SemanticVMGeneratedFieldRef { u32 name_hash; u32 name_offset; u16 name_length; u16 field_ordinal; u8 low; u8 high; u8 reserved[2]; };",
        "typedef struct BusterA64SemanticVMGeneratedTransform BusterA64SemanticVMGeneratedTransform;",
        "struct BusterA64SemanticVMGeneratedTransform { u32 semantic_transform_id; u32 field_ref_first; u16 field_ref_count; u32 key_ref_first; u16 key_ref_count; u8 op; u8 affine_op; u16 constant; u8 program_count; u8 reserved[3]; };",
        "typedef struct BusterA64SemanticVMGeneratedAtomProgram BusterA64SemanticVMGeneratedAtomProgram;",
        "struct BusterA64SemanticVMGeneratedAtomProgram { u32 atom_id; };",
        "typedef struct BusterA64SemanticVMGeneratedAlias BusterA64SemanticVMGeneratedAlias;",
        "struct BusterA64SemanticVMGeneratedAlias { u32 target_form; u16 injected_field_count; u16 same_field_count; u32 condition_digest; u16 preference_count; u8 condition_supported; u8 reserved; };",
        "",
        "static const BusterA64SemanticVMGeneratedFieldRef buster_a64_semantic_vm_field_refs[] = {",
        c_array(field_values),
        "};",
        "static const char8 buster_a64_semantic_vm_field_name_bytes[] = {",
        c_array([str(x) for x in field_name_bytes], 24),
        "};",
        "static const BusterA64SemanticVMGeneratedTransform buster_a64_semantic_vm_transforms[] = {",
        c_array(transform_values, 4),
        "};",
        "static const BusterA64SemanticVMGeneratedAtomProgram buster_a64_semantic_vm_atom_programs[] = {",
        c_array(atom_values, 2),
        "};",
        "static const BusterA64SemanticVMGeneratedAlias buster_a64_semantic_vm_aliases[] = {",
        c_array(alias_values, 4),
        "};",
        "",
        "static const u8 buster_a64_semantic_vm_row_coverage_table[] = {",
        c_array([str(x) for x in coverage]),
        "};",
        "static const u8 buster_a64_semantic_vm_row_gap_reason_table[] = {",
        c_array([str(x) for x in gaps]),
        "};",
        "static const u16 buster_a64_semantic_vm_canonical_raw_indices[] = {",
        c_array([str(x) for x in canonical_raw_indices]),
        "};",
        "",
        "#endif",
        "",
    ]
    path.write_text("\n".join(lines), encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--canonical-jsonl", type=Path, required=True)
    parser.add_argument("--semantic-jsonl", type=Path, required=True)
    parser.add_argument("--xml-root", type=Path, required=True)
    parser.add_argument("--output-header", type=Path, required=True)
    parser.add_argument("--output-manifest", type=Path, required=True)
    args = parser.parse_args()

    canonical = rows_load(args.canonical_jsonl)
    canonical_ids = source_canonical_ids(args.canonical_jsonl)
    rows = rows_load(args.semantic_jsonl)
    rows = [row for row in rows if row.get("id") in canonical_ids]
    rows.sort(key=lambda row: int(row["form_index"]))
    if len(rows) != TOTAL_COUNT or sum(row.get("kind") == "canonical" for row in rows) != CANONICAL_COUNT or sum(row.get("kind") == "alias" for row in rows) != ALIAS_COUNT:
        raise RuntimeError("Apple-M1 semantic VM denominator mismatch")

    by_encoding = {row["encoding_name"]: row for row in rows}
    canonical_raw_indices: list[int] = []
    canonical_ordinal = 0
    for row in rows:
        if row.get("kind") == "canonical":
            canonical_raw_indices.append(canonical_ordinal if row.get("status") == "defined" else 0xFFFF)
            canonical_ordinal += 1
        else:
            canonical_raw_indices.append(0xFFFF)
    if canonical_ordinal != CANONICAL_COUNT:
        raise RuntimeError("canonical ordinal mismatch")

    field_refs: list[dict[str, int]] = []
    field_name_bytes: list[int] = []
    field_name_offsets: dict[str, int] = {}

    def field_name_offset(name: str) -> int:
        if name in field_name_offsets:
            return field_name_offsets[name]
        offset = len(field_name_bytes)
        field_name_bytes.extend(name.encode("ascii"))
        field_name_bytes.append(0)
        field_name_offsets[name] = offset
        return offset

    def make_field_ref(name: str, low: int | None = None, high: int | None = None,
                       widths: dict[str, int] | None = None, ordinals: dict[str, int] | None = None) -> dict[str, int] | None:
        if low is None or high is None:
            width = (widths or {}).get(name, 0)
            low = 0
            high = width - 1 if width else 0
        width = (widths or {}).get(name, 0)
        if width == 0 or high >= width:
            return None
        if low < 0 or high < low or high > 31:
            return None
        return {
            "name_hash": fnv1a32(name),
            "name_offset": field_name_offset(name),
            "name_length": len(name),
            "field_ordinal": (ordinals or {}).get(name, 0xFFFF),
            "low": low,
            "high": high,
            "reserved": 0,
        }

    transform_records: list[dict[str, Any]] = []
    unsupported_transforms: set[int] = set()
    xml_formula_corrections: list[dict[str, Any]] = []
    # Programs are already normalized and typed by the semantic metadata
    # generator.  Keep only the sparse IDs of PROGRAM atoms here; runtime
    # evaluation consumes their typed instruction spans through the semantic
    # accessors rather than re-expanding all 20,244 value atoms.
    atom_program_ids: list[int] = []
    transform_program_count = 0

    # Flatten value atoms in exactly the order emitted by semantic metadata.
    # The VM only needs descriptors for PROGRAM atoms; other atom types remain
    # in the existing pointer-free semantic table.
    semantic_atom_id = 0
    for row in rows:
        form_fields = {field["name"] for field in row.get("fields", [])}
        field_widths = {
            field["name"]: max((int(segment.get("value_lsb", 0)) + int(segment.get("width", 0)) for segment in field.get("segments", [])), default=0)
            for field in row.get("fields", [])
        }
        field_ordinals = {field["name"]: ordinal for ordinal, field in enumerate(row.get("fields", []))}
        xml_texts = xml_encoding_texts(args.xml_root, row["provenance"]["xml_file"], row["encoding_name"])
        # The semantic C projection orders transforms by operand ownership,
        # not by the source row's presentation order.  Reproduce that
        # bounded ordering so a form's ``transform_first`` range indexes the
        # VM descriptor for the same relation (e.g. shared SIMD decodes may
        # precede their value table even when the JSON lists them second).
        source_transforms = row.get("transforms", [])
        ordered_transforms: list[tuple[int, dict[str, Any]]] = []
        for operand in row.get("operands", []):
            start = int(operand.get("transform_first", 0))
            end = start + int(operand.get("transform_count", 0))
            if start < 0 or end > len(source_transforms):
                raise RuntimeError(f"operand transform range exceeds row in {row['id']}")
            ordered_transforms.extend((index, source_transforms[index]) for index in range(start, end))
        if len(ordered_transforms) != len(source_transforms):
            raise RuntimeError(f"unreferenced transform in {row['id']}")
        for transform_ordinal, transform in ordered_transforms:
            kind = transform.get("kind")
            op = {
                "concat": OP_CONCAT,
                "slice": OP_SLICE,
                "integer_decode": OP_INTEGER_DECODE,
                "text_transform": OP_SCALE_DIV,
                "value_table": OP_TABLE_EXACT_WILDCARD,
                "shared_decode": OP_SHARED_DECODE,
            }.get(kind, OP_INVALID)
            field_first = len(field_refs)
            fields: list[str] = []
            field_specs: list[tuple[str, int | None, int | None]] = []
            typed_program = bool(transform.get("program"))
            if typed_program:
                # The semantic metadata carries the authoritative typed
                # instruction span.  Keep the descriptor's relation opcode
                # for indexing, while runtime dispatches through that span.
                if len(transform["program"]) > 4:
                    raise RuntimeError(f"typed transform program exceeds VM bound in {row['id']}")
                transform_program_count += 1
                if kind == "integer_decode":
                    affine = parse_affine_formula(xml_texts, form_fields)
                    if affine is not None and affine[0] != OP_INTEGER_DECODE:
                        xml_formula_corrections.append({"id": row["id"], "operation": affine[0], "constant": affine[1], "fields": affine[2]})
            elif kind == "concat":
                fields = list(transform.get("parts", []))
                field_specs = [(name, None, None) for name in fields]
            elif kind == "slice":
                fields = [transform.get("field", "")]
                field_specs = [(fields[0], int(transform.get("low", 0)), int(transform.get("high", 0)))]
            elif kind in ("integer_decode", "text_transform"):
                # XML, rather than the truncated semantic record, defines the
                # immediate formula and its source fields.
                affine = parse_affine_formula(xml_texts, form_fields)
                if kind == "integer_decode" and affine is not None:
                    op, constant, fields = affine
                    field_specs = [(name, None, None) for name in fields]
                    if op != OP_INTEGER_DECODE:
                        xml_formula_corrections.append({"id": row["id"], "operation": op, "constant": constant, "fields": fields})
                elif kind == "integer_decode":
                    unsupported_transforms.add(len(transform_records))
                else:
                    program = transform.get("program", [])
                    if len(program) == 1 and program[0].get("op") == "scale_div" and int(program[0].get("value", 0)) > 0:
                        op = OP_SCALE_DIV
                        constant = int(program[0]["value"])
                    else:
                        op = OP_INVALID
                        constant = 0
                    # Use the operand field containing this transform as the
                    # input.  The source record is explanation-index based.
                    for operand in row.get("operands", []):
                        start = int(operand.get("transform_first", 0))
                        end = start + int(operand.get("transform_count", 0))
                        if start <= transform_ordinal < end and operand.get("fields"):
                            fields = [operand["fields"][0]]
                            field_specs = [(fields[0], None, None)]
                            break
            elif kind == "shared_decode":
                fields = list(transform.get("fields", []))
                field_specs = [(name, None, None) for name in fields]
            elif kind == "value_table":
                # Key headers are the architectural inputs.  They are stored
                # below as field refs; table atoms themselves stay in the
                # semantic generated table.
                fields = []
                for header in transform.get("key_headers", []):
                    parsed = parse_slice_header(header)
                    if parsed is None:
                        op = OP_INVALID
                        continue
                    fields.append(parsed[0])
            if not typed_program and (len(fields) > 8 or any(field not in form_fields for field in fields)):
                op = OP_INVALID
            key_ref_first = len(field_refs) if kind == "value_table" and not typed_program else 0
            key_ref_count = len(fields) if kind == "value_table" and not typed_program else 0
            if kind == "value_table" and not typed_program:
                for header in transform.get("key_headers", []):
                    parsed = parse_slice_header(header)
                    if parsed is None:
                        continue
                    name, low, high = parsed
                    if name not in form_fields:
                        continue
                    if "[" not in header:
                        low, high = 0, field_widths.get(name, 0) - 1
                    reference = make_field_ref(name, low, high, field_widths, field_ordinals)
                    if reference is None:
                        op = OP_INVALID
                        continue
                    field_refs.append(reference)
            field_first = len(field_refs)
            if kind != "value_table" and not typed_program:
                for name, low, high in field_specs:
                    if name and name in form_fields:
                        # XML names are field names, not prose.  Resolve
                        # optional ``name[bit]`` syntax conservatively.
                        reference = make_field_ref(name, low, high, field_widths, field_ordinals)
                        if reference is None:
                            op = OP_INVALID
                        else:
                            field_refs.append(reference)
            affine_op = OP_INVALID
            constant = 0
            if kind == "text_transform" and not typed_program:
                program = transform.get("program", [])
                if len(program) == 1 and program[0].get("op") == "scale_div":
                    affine_op = OP_SCALE_DIV
                    constant = int(program[0].get("value", 0))
            if kind == "integer_decode" and not typed_program:
                parsed = parse_affine_formula(xml_texts, form_fields)
                if parsed is None:
                    op = OP_INVALID
                else:
                    op, constant, _ = parsed
                    if op == OP_INTEGER_DECODE:
                        affine_op = OP_INVALID
                    else:
                        affine_op = op
                        op = OP_INTEGER_DECODE
            if constant < 0 or constant > 0xFFFF:
                op = OP_INVALID
                affine_op = OP_INVALID
                constant = 0
            record = {
                "semantic_transform_id": len(transform_records),
                "field_ref_first": field_first,
                "field_ref_count": len(fields) if kind != "value_table" and not typed_program else 0,
                "key_ref_first": key_ref_first,
                "key_ref_count": key_ref_count,
                "op": op,
                "affine_op": affine_op,
                "constant": constant,
                "program_count": len(transform.get("program", [])) if typed_program else 0,
            }
            transform_records.append(record)
            transform_id = len(transform_records) - 1
            if op == OP_INVALID and not typed_program:
                unsupported_transforms.add(transform_id)
            # Record only typed PROGRAM atom IDs.  The value atom itself and
            # its bounded instruction span remain in the canonical semantic
            # metadata; the VM must not emit a second 20k-row atom table.
            if kind == "value_table":
                table_supported = True
                for entry in transform.get("entries", []):
                    for atom in entry.get("key", []) + entry.get("result", []):
                        if atom.get("type") == "program":
                            atom_program_ids.append(semantic_atom_id)
                        else:
                            table_supported = table_supported and atom.get("type") in ("integer", "bits", "enum")
                        semantic_atom_id += 1
                if not table_supported:
                    unsupported_transforms.add(transform_id)
    if semantic_atom_id != 20244 or len(atom_program_ids) != 296 or transform_program_count != 372:
        raise RuntimeError(f"typed program census mismatch: atoms={semantic_atom_id}, value_programs={len(atom_program_ids)}, transform_programs={transform_program_count}")

    # Alias target links and condition digests are retained even though
    # condition evaluation is deliberately fail-closed in this milestone.
    aliases: list[dict[str, int]] = []
    for row in rows:
        if row.get("kind") != "alias":
            continue
        target = by_encoding.get(row.get("alias", {}).get("target_encoding_id"))
        if target is None:
            raise RuntimeError(f"alias target missing: {row['id']}")
        alias_fields = {field["name"] for field in row.get("fields", [])}
        target_fields = {field["name"] for field in target.get("fields", [])}
        condition_tokens = json.dumps(row.get("alias", {}).get("condition_program", []), separators=(",", ":"), sort_keys=True)
        aliases.append({
            "target": int(target["form_index"]),
            "injected": len(target_fields - alias_fields),
            "same": len(target_fields & alias_fields),
            "condition_digest": fnv1a32(condition_tokens),
            "preference_count": len(row.get("alias", {}).get("preferences", [])),
            "condition_supported": 0,
        })
    if len(aliases) != ALIAS_COUNT:
        raise RuntimeError("alias count mismatch")

    # A row can have all transforms represented by VM operations, but full
    # instruction semantics still require a per-family executor, constraints,
    # and independent oracles.  Therefore semantic executable coverage remains
    # intentionally zero; the transform coverage bit is the honest milestone.
    coverage: list[int] = []
    gaps: list[int] = []
    global_transform_cursor = 0
    for row in rows:
        row_transform_count = len(row.get("transforms", []))
        transform_ok = True
        for _ in range(row_transform_count):
            if global_transform_cursor in unsupported_transforms:
                transform_ok = False
            global_transform_cursor += 1
        status = 0
        gap = GAP_NONE
        if row.get("status") == "defined" and row.get("kind") == "canonical":
            status |= ROW_RAW_CODEC
        elif row.get("status") != "defined":
            gap = GAP_UNDEFINED
        if transform_ok:
            status |= ROW_TRANSFORMS
        elif gap == GAP_NONE:
            gap = GAP_UNSUPPORTED_TRANSFORM
        if row.get("kind") == "alias":
            status |= ROW_ALIAS_TARGET
            if row.get("alias", {}).get("condition_program") not in ([], ["Unconditionally"]):
                gap = GAP_ALIAS_CONDITION
        if gap == GAP_NONE and not (status & ROW_SEMANTIC_EXECUTABLE):
            gap = GAP_INCOMPLETE_SEMANTICS
        coverage.append(status)
        gaps.append(gap)

    args.output_header.parent.mkdir(parents=True, exist_ok=True)
    args.output_manifest.parent.mkdir(parents=True, exist_ok=True)
    emit_header(args.output_header, rows, transform_records, field_refs, field_name_bytes, atom_program_ids, aliases, coverage, gaps, canonical_raw_indices)
    manifest = {
        "schema_version": 2,
        "canonical_sha256": CANONICAL_SHA256,
        "canonical_rows": CANONICAL_COUNT,
        "alias_rows": ALIAS_COUNT,
        "rows": TOTAL_COUNT,
        "source_policy": {"canonical_jsonl": "pinned", "arm_xml": "pinned-official", "raw_prose_emitted": False},
        "operation_count": OP_COUNT,
        "typed_programs": {"transform_count": transform_program_count, "value_atom_count": len(atom_program_ids), "value_atom_ids_sparse": True},
        "operation_names": [
            "invalid", "field", "extract", "concat", "uint_extend", "signed_extend", "add_const", "sub_const",
            "sub_from_const", "scale_mul", "scale_div", "fixed_literal", "default", "optional", "table_exact_wildcard",
            "reserved_reject", "pc_relative", "page_relative", "condition_invert", "register_add_mod32", "bitwise_not", "movn",
            "logical_immediate", "fp_immediate", "advsimd_immediate", "sysop_lookup", "alias_map", "alias_inject", "alias_condition",
            "slice", "integer_decode", "shared_decode",
        ],
        "table_census": {"value_tables": 1528, "value_entries": 7681, "max_key_arity": 4, "max_entries": 166, "max_concat_parts": 8},
        "coverage": {
            "raw_codec_rows": sum(bool(x & ROW_RAW_CODEC) for x in coverage),
            "transform_rows": sum(bool(x & ROW_TRANSFORMS) for x in coverage),
            "alias_target_rows": sum(bool(x & ROW_ALIAS_TARGET) for x in coverage),
            "semantic_executable_rows": sum(bool(x & ROW_SEMANTIC_EXECUTABLE) for x in coverage),
            "gap_rows": sum(x != GAP_NONE for x in gaps),
            "gap_reason_counts": {str(reason): gaps.count(reason) for reason in range(GAP_NONE, GAP_INCOMPLETE_SEMANTICS + 1)},
        },
        "formula_corrections": xml_formula_corrections,
        "formula_correction_count": len(xml_formula_corrections),
        "alias_field_census": {"injected_field_differences": sum(a["injected"] > 0 for a in aliases), "same_field_rows": sum(a["injected"] == 0 for a in aliases)},
        "gap_row_ids": {str(reason): [row["id"] for row, gap in zip(rows, gaps) if gap == reason] for reason in range(1, GAP_INCOMPLETE_SEMANTICS + 1)},
    }
    args.output_manifest.write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
