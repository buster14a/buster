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

VM_SCHEMA_VERSION = 3
PROJECTION_FIXED = 1 << 0

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


def xml_encoding_layout(xml_root: Path, xml_file: str, encoding_name: str) -> dict[str, dict[int, dict[str, int | str | None]]]:
    """Return logical field bits from the encoding's XML regdiagram.

    The canonical importer intentionally drops fixed bits from a field's
    compact value.  Arm's XML boxes retain the logical field positions,
    including those fixed bits, so table keys must be projected through this
    layout before the VM can match them.
    """
    path = xml_root / xml_file
    if not path.is_file():
        raise RuntimeError(f"missing pinned Arm XML source: {path}")
    root = ET.parse(path).getroot()
    for iclass in root.iter():
        if local_name(iclass.tag) != "iclass":
            continue
        regdiagram = next((child for child in iclass if local_name(child.tag) == "regdiagram"), None)
        if regdiagram is None:
            continue
        for encoding in iclass:
            if local_name(encoding.tag) != "encoding" or encoding.attrib.get("name") != encoding_name:
                continue
            result: dict[str, dict[int, dict[str, int | str | None]]] = {}
            for box in regdiagram:
                if local_name(box.tag) != "box" or not box.attrib.get("name"):
                    continue
                name = box.attrib["name"]
                try:
                    width = int(box.attrib.get("width", "1"))
                    hibit = int(box.attrib["hibit"])
                except (KeyError, ValueError) as error:
                    raise RuntimeError(f"invalid XML field box in {path}: {box.attrib!r}") from error
                if width <= 0 or hibit < width - 1 or hibit >= 32:
                    raise RuntimeError(f"invalid XML field box in {path}: {box.attrib!r}")
                low = hibit - width + 1
                pattern = box.attrib.get("psbits")
                if pattern is not None and len(pattern) != width:
                    pattern = None
                field = result.setdefault(name, {})
                for logical_bit in range(width):
                    instruction_lsb = low + logical_bit
                    fixed = None
                    if pattern is not None:
                        candidate = pattern[width - 1 - logical_bit]
                        if candidate in "01":
                            fixed = candidate
                    previous = field.get(logical_bit)
                    bit = {"instruction_lsb": instruction_lsb, "fixed": fixed}
                    if previous is not None and previous != bit:
                        raise RuntimeError(f"duplicate XML logical field bit: {path}:{encoding_name}:{name}[{logical_bit}]")
                    field[logical_bit] = bit
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


def parse_fixed_integer(row: dict[str, Any], key: str) -> int:
    value = row.get(key, 0)
    if isinstance(value, int):
        return value
    if not isinstance(value, str):
        raise RuntimeError(f"invalid {key} in {row.get('id', '<unknown>')}")
    try:
        return int(value, 0)
    except ValueError as error:
        raise RuntimeError(f"invalid {key} in {row.get('id', '<unknown>')}: {value!r}") from error


def canonical_field_bit_map(row: dict[str, Any]) -> dict[int, tuple[str, int]]:
    """Map instruction bits to compact canonical field bits.

    The canonical importer intentionally packs only variable bits into a
    field's value.  Keeping this map at generator time lets the VM retain the
    compact field ABI while matching table keys in the XML field's logical
    bit numbering.
    """
    result: dict[int, tuple[str, int]] = {}
    for field in row.get("fields", []):
        name = field.get("name")
        if not isinstance(name, str) or not name:
            raise RuntimeError(f"field without a name in {row.get('id', '<unknown>')}")
        for segment in field.get("segments", []):
            try:
                instruction_lsb = int(segment["instruction_lsb"])
                value_lsb = int(segment["value_lsb"])
                width = int(segment["width"])
            except (KeyError, TypeError, ValueError) as error:
                raise RuntimeError(f"invalid canonical field segment in {row.get('id', '<unknown>')}: {segment!r}") from error
            if instruction_lsb < 0 or value_lsb < 0 or width <= 0 or instruction_lsb + width > 32 or value_lsb + width > 32:
                raise RuntimeError(f"canonical field segment out of bounds in {row.get('id', '<unknown>')}: {segment!r}")
            for offset in range(width):
                instruction_bit = instruction_lsb + offset
                value_bit = value_lsb + offset
                previous = result.get(instruction_bit)
                current = (name, value_bit)
                if previous is not None and previous != current:
                    raise RuntimeError(f"canonical field segment overlap in {row.get('id', '<unknown>')} at bit {instruction_bit}")
                result[instruction_bit] = current
    return result


def value_atom_integer(atom: dict[str, Any], row_id: str, header: str) -> int | None:
    if atom.get("type") != "integer":
        return None
    value = atom.get("value")
    if isinstance(value, bool) or not isinstance(value, int):
        raise RuntimeError(f"invalid integer table key in {row_id}:{header}: {atom!r}")
    return value


def validate_projection_atom(atom: dict[str, Any], projection: list[dict[str, int]], logical_width: int,
                             row_id: str, header: str) -> None:
    """Validate key atom width and its fixed logical bits.

    This runs before any generated output is written.  A malformed fixed-bit
    key therefore fails the whole generation transaction instead of silently
    producing a partially usable VM table.
    """
    atom_type = atom.get("type")
    if atom_type == "bits":
        text = atom.get("value")
        if not isinstance(text, str) or len(text) != logical_width:
            raise RuntimeError(f"table key width mismatch in {row_id}:{header}: expected {logical_width}, got {text!r}")
        for bit in projection:
            if not (bit["flags"] & PROJECTION_FIXED):
                continue
            character = text[logical_width - 1 - bit["logical_bit"]]
            if character in "01" and int(character) != bit["fixed_value"]:
                raise RuntimeError(f"table key fixed-bit mismatch in {row_id}:{header}: {text!r}")
            if character not in "01xX":
                raise RuntimeError(f"invalid bit table key in {row_id}:{header}: {text!r}")
        return
    if atom_type == "integer":
        value = value_atom_integer(atom, row_id, header)
        assert value is not None
        if value < 0 or (logical_width < 63 and value >= (1 << logical_width)) or (logical_width == 63 and value > ((1 << 63) - 1)):
            raise RuntimeError(f"table integer key out of range in {row_id}:{header}: {value}")
        for bit in projection:
            if bit["flags"] & PROJECTION_FIXED and ((value >> bit["logical_bit"]) & 1) != bit["fixed_value"]:
                raise RuntimeError(f"table key fixed-bit mismatch in {row_id}:{header}: {value}")
        return
    raise RuntimeError(f"unsupported table key atom in {row_id}:{header}: {atom_type!r}")


def table_key_projection(row: dict[str, Any], header: str, xml_layout: dict[str, dict[int, dict[str, int | str | None]]]) -> tuple[str, int, int, int, list[dict[str, int]]]:
    """Project one logical XML key slice onto a compact canonical field.

    The returned field range is in canonical compact-value bits.  Each
    projection entry records the corresponding logical key bit and either a
    compact variable bit or a fixed instruction bit value.
    """
    parsed = parse_slice_header(header)
    if parsed is None:
        raise RuntimeError(f"invalid table key header: {row.get('id', '<unknown>')}:{header!r}")
    name, logical_low, logical_high = parsed
    layout = xml_layout.get(name)
    if not layout:
        raise RuntimeError(f"XML field {name!r} missing for table key {row.get('id', '<unknown>')}:{header!r}")
    if "[" not in header:
        logical_low, logical_high = min(layout), max(layout)
    if logical_low < 0 or logical_high < logical_low or logical_high > 31:
        raise RuntimeError(f"invalid logical table key range {row.get('id', '<unknown>')}:{header!r}")
    canonical_bits = canonical_field_bit_map(row)
    fixed_mask = parse_fixed_integer(row, "fixed_mask")
    fixed_value = parse_fixed_integer(row, "fixed_value")
    projection: list[dict[str, int]] = []
    variable_names: set[str] = set()
    variable_bits: list[int] = []
    for logical_bit in range(logical_low, logical_high + 1):
        box = layout.get(logical_bit)
        if box is None:
            raise RuntimeError(f"logical XML bit {name}[{logical_bit}] missing for {row.get('id', '<unknown>')}:{header!r}")
        instruction_bit = int(box["instruction_lsb"])
        variable = canonical_bits.get(instruction_bit)
        item = {"logical_bit": logical_bit - logical_low, "actual_bit": 0xFF, "fixed_value": 0, "flags": 0}
        if variable is not None:
            variable_name, actual_bit = variable
            variable_names.add(variable_name)
            variable_bits.append(actual_bit)
            item["actual_bit"] = actual_bit
        elif fixed_mask & (1 << instruction_bit):
            item["fixed_value"] = (fixed_value >> instruction_bit) & 1
            item["flags"] = PROJECTION_FIXED
        else:
            raise RuntimeError(f"unmapped logical XML bit {name}[{logical_bit}] at instruction bit {instruction_bit} for {row.get('id', '<unknown>')}:{header!r}")
        projection.append(item)
    if len(variable_names) != 1 or not variable_bits:
        raise RuntimeError(f"table key {row.get('id', '<unknown>')}:{header!r} does not project to one canonical field")
    actual_low = min(variable_bits)
    actual_high = max(variable_bits)
    if actual_low < 0 or actual_high > 31 or actual_high < actual_low:
        raise RuntimeError(f"invalid compact table key range {row.get('id', '<unknown>')}:{header!r}")
    return next(iter(variable_names)), actual_low, actual_high, logical_low, projection


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
    projection_bits: list[dict[str, int]],
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
    field_values = [
        "{%u, %u, %u, %u, %u, %u, %u, %u, %u, %u}"
        % (
            x["name_hash"], x["name_offset"], x["name_length"], x["field_ordinal"],
            x["low"], x["high"], x["projection_first"], x["projection_count"],
            x["logical_low"], x["logical_high"],
        )
        for x in field_refs
    ]
    projection_values = ["{%u, %u, %u, %u}" % (x["logical_bit"], x["actual_bit"], x["fixed_value"], x["flags"]) for x in projection_bits]
    atom_values = ["{%u}" % atom_id for atom_id in atom_program_ids]
    alias_values = ["{%u, %u, %u, %u, %u, %u, 0}" % (a["target"], a["injected"], a["same"], a["condition_digest"], a["preference_count"], a["condition_supported"]) for a in aliases]
    lines = [
        "/* Generated by generate_aarch64_semantic_vm.py; do not edit. */",
        "#ifndef BUSTER_AARCH64_SEMANTIC_VM_GENERATED_H",
        "#define BUSTER_AARCH64_SEMANTIC_VM_GENERATED_H",
        "#include <buster/lib/base.h>",
        "",
        f"#define BUSTER_AARCH64_SEMANTIC_VM_SCHEMA_VERSION {VM_SCHEMA_VERSION}u",
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
        f"#define BUSTER_AARCH64_SEMANTIC_VM_PROJECTION_BIT_COUNT {len(projection_bits)}u",
        f"#define BUSTER_AARCH64_SEMANTIC_VM_PROJECTION_FIXED {PROJECTION_FIXED}u",
        "typedef struct BusterA64SemanticVMGeneratedFieldRef BusterA64SemanticVMGeneratedFieldRef;",
        "struct BusterA64SemanticVMGeneratedFieldRef { u32 name_hash; u32 name_offset; u16 name_length; u16 field_ordinal; u8 low; u8 high; u32 projection_first; u8 projection_count; u8 logical_low; u8 logical_high; };",
        "typedef struct BusterA64SemanticVMGeneratedProjectionBit BusterA64SemanticVMGeneratedProjectionBit;",
        "struct BusterA64SemanticVMGeneratedProjectionBit { u8 logical_bit; u8 actual_bit; u8 fixed_value; u8 flags; };",
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
        "static const BusterA64SemanticVMGeneratedProjectionBit buster_a64_semantic_vm_projection_bits[] = {",
        c_array(projection_values, 8),
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
    projection_bits: list[dict[str, int]] = []
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
                       widths: dict[str, int] | None = None, ordinals: dict[str, int] | None = None,
                       logical_low: int = 0, logical_high: int = 0,
                       projection_first: int = 0, projection_count: int = 0) -> dict[str, int] | None:
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
            "projection_first": projection_first,
            "projection_count": projection_count,
            "logical_low": logical_low,
            "logical_high": logical_high,
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
        xml_layout = xml_encoding_layout(args.xml_root, row["provenance"]["xml_file"], row["encoding_name"])
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
                    name, _, _ = parsed
                    if name not in form_fields:
                        op = OP_INVALID
                        continue
                    projected_name, low, high, logical_low, projection = table_key_projection(row, header, xml_layout)
                    logical_high = logical_low + len(projection) - 1
                    projection_first = len(projection_bits)
                    projection_bits.extend(projection)
                    reference = make_field_ref(projected_name, low, high, field_widths, field_ordinals,
                                               logical_low, logical_high, projection_first, len(projection))
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
                entries = transform.get("entries", [])
                if not typed_program and len(fields) != key_ref_count:
                    raise RuntimeError(f"table key/ref count mismatch in {row['id']}")
                for entry in entries:
                    if not typed_program and len(entry.get("key", [])) != key_ref_count:
                        raise RuntimeError(f"table entry key count mismatch in {row['id']}")
                    if not typed_program:
                        for key_index, atom in enumerate(entry.get("key", [])):
                            reference = field_refs[key_ref_first + key_index]
                            first = reference["projection_first"]
                            last = first + reference["projection_count"]
                            validate_projection_atom(atom, projection_bits[first:last],
                                                     reference["logical_high"] - reference["logical_low"] + 1,
                                                     row["id"], transform.get("key_headers", [])[key_index])
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
    emit_header(args.output_header, rows, transform_records, field_refs, projection_bits, field_name_bytes, atom_program_ids, aliases, coverage, gaps, canonical_raw_indices)
    manifest = {
        "schema_version": VM_SCHEMA_VERSION,
        "canonical_sha256": CANONICAL_SHA256,
        "canonical_rows": CANONICAL_COUNT,
        "alias_rows": ALIAS_COUNT,
        "rows": TOTAL_COUNT,
        "source_policy": {"canonical_jsonl": "pinned", "arm_xml": "pinned-official", "raw_prose_emitted": False},
        "operation_count": OP_COUNT,
        "projection": {
            "field_ref_count": len(field_refs),
            "projection_bit_count": len(projection_bits),
            "projected_table_key_count": sum(record["key_ref_count"] for record in transform_records if record["op"] == OP_TABLE_EXACT_WILDCARD),
        },
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
