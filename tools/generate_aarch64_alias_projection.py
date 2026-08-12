#!/usr/bin/env python3
"""Generate the bounded Apple-M1 A64 alias projection.

The semantic JSONL is the only source of truth.  This projection deliberately
does not contain hand-written mnemonic cases: aliases are linked to their
canonical target by ``alias.target_encoding_id`` and all field/condition
programs are copied from the source row.  The generated C table is a compact
index used by the runtime; the JSONL and manifest are the audit projection.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
from typing import Any


CANONICAL_SHA256 = "8485c5c61835d5394d325757ab2964890e8bdfea304c6faa8fd4c23e4c7aabec"
ALIAS_DENOMINATOR_SHA256 = "fa030a65c5be661813f50a92f3027e152ab9c1b55701f851c6142390df5c25a8"
SCHEMA_VERSION = 1
FORM_COUNT = 1695
CANONICAL_COUNT = 1523
ALIAS_COUNT = 172
MAX_OPERANDS = 8
MAX_FIELDS = 16
MAX_CONDITION_TOKENS = 64
OWNER_CENSUS = {
    "memory": 64,
    "scalar_integer": 59,
    "direct_gpr": 21,
    "general_nonmemory": 10,
    "system": 9,
    "complex_simd_fp": 7,
    "direct_simd": 2,
}


def generic_condition_supported(program: list[str]) -> bool:
    return (len(program) == 1 and program[0] in {"Unconditionally", "Never"}) or (
        len(program) == 3 and program[1] in {"==", "!="}
    )


def generic_operand_projection_supported(row: dict[str, Any]) -> bool:
    return all(int(operand.get("transform_count", 0)) == 0 and len(operand.get("fields", [])) == 1 and
               "simd_lane" not in operand.get("kinds", []) for operand in row.get("operands", []))


def generic_projection_executable(row: dict[str, Any]) -> bool:
    program = list((row.get("alias") or {}).get("condition_program", []))
    return generic_condition_supported(program) and program != ["Never"] and generic_operand_projection_supported(row)


def digest(value: str) -> int:
    return int(value, 0)


def source_digest(rows: list[dict[str, Any]]) -> str:
    ordered = sorted(rows, key=lambda row: row["id"])
    payload = "".join(f"{row['id']}\t{row['source_digest']}\n" for row in ordered).encode()
    return hashlib.sha256(payload).hexdigest()


def check_segments(row: dict[str, Any]) -> None:
    for field in row.get("fields", []):
        total = 0
        for segment in field.get("segments", []):
            width = int(segment["width"])
            instruction_lsb = int(segment["instruction_lsb"])
            value_lsb = int(segment.get("value_lsb", 0))
            if width <= 0 or width > 32 or instruction_lsb < 0 or instruction_lsb + width > 32:
                raise SystemExit(f"invalid alias field segment in {row['id']}: {field['name']}")
            if value_lsb < 0 or value_lsb + width > 32:
                raise SystemExit(f"invalid alias value segment in {row['id']}: {field['name']}")
            total = max(total, value_lsb + width)
        # The source width is implicit in segment value_lsb.  Overlays are
        # valid, so only reject layouts that make a field wider than 32 bits.
        if total > 32:
            raise SystemExit(f"alias field exceeds 32 bits in {row['id']}: {field['name']}")


def load(path: Path) -> tuple[list[dict[str, Any]], list[dict[str, Any]], dict[str, dict[str, Any]]]:
    rows = [json.loads(line) for line in path.read_text().splitlines() if line.strip()]
    if len(rows) != FORM_COUNT:
        raise SystemExit(f"semantic form count changed: {len(rows)} != {FORM_COUNT}")
    canonical = [row for row in rows if row.get("kind") == "canonical"]
    aliases = [row for row in rows if row.get("kind") == "alias"]
    if len(canonical) != CANONICAL_COUNT or len(aliases) != ALIAS_COUNT:
        raise SystemExit(f"semantic denominator changed: canonical={len(canonical)} alias={len(aliases)}")
    if source_digest(aliases) != ALIAS_DENOMINATOR_SHA256:
        raise SystemExit("alias denominator digest changed")
    if len({int(row["form_index"]) for row in rows}) != FORM_COUNT:
        raise SystemExit("semantic form indexes are not unique")
    canonical_by_name = {row["encoding_name"]: row for row in canonical}
    if len(canonical_by_name) != len(canonical):
        raise SystemExit("canonical encoding names are not unique")
    for row in aliases:
        alias = row.get("alias") or {}
        target_name = alias.get("target_encoding_id")
        target = canonical_by_name.get(target_name)
        if target is None:
            raise SystemExit(f"alias target is not canonical: {row['id']} -> {target_name}")
        if len(row.get("operands", [])) > MAX_OPERANDS or len(row.get("fields", [])) > MAX_FIELDS:
            raise SystemExit(f"alias shape exceeds bounded ABI: {row['id']}")
        if len(alias.get("condition_program", [])) > MAX_CONDITION_TOKENS or len(alias.get("preference_condition_program", [])) > MAX_CONDITION_TOKENS:
            raise SystemExit(f"alias predicate exceeds bounded ABI: {row['id']}")
        if alias.get("preferences"):
            raise SystemExit(f"unprojected nested alias preferences: {row['id']}")
        check_segments(row)
        # Every target-only field must be fixed by the alias mask.  This is the
        # invariant that makes alias-word construction transactional and avoids
        # any target-row or mnemonic special cases in C.
        alias_fields = {field["name"] for field in row.get("fields", [])}
        alias_fixed_mask = int(row["fixed_mask"], 0)
        target_fixed_mask = int(target["fixed_mask"], 0)
        if (alias_fixed_mask & target_fixed_mask) != target_fixed_mask:
            raise SystemExit(f"alias fixed mask does not cover target: {row['id']}")
        target_fields = {field["name"] for field in target.get("fields", [])}
        if target_fields - alias_fields:
            # A target-only field is allowed only when all of its instruction
            # bits are fixed in the alias row.  ``field_mask`` is the source
            # mask emitted by the importer and is safer than reconstructing a
            # mask from a possibly overlaid segment list.
            alias_bits = 0
            for field in row.get("fields", []):
                alias_bits |= sum(((1 << int(seg["width"])) - 1) << int(seg["instruction_lsb"]) for seg in field.get("segments", []))
            for field in target.get("fields", []):
                if field["name"] in alias_fields:
                    continue
                target_bits = sum(((1 << int(seg["width"])) - 1) << int(seg["instruction_lsb"]) for seg in field.get("segments", []))
                if target_bits & ~alias_bits & ~alias_fixed_mask:
                    raise SystemExit(f"target-only non-fixed field: {row['id']} {field['name']}")
        row["_target"] = target
    aliases.sort(key=lambda row: int(row["form_index"]))
    return canonical, aliases, canonical_by_name


def owner_name(row: dict[str, Any]) -> str:
    return str(row.get("owner", "unknown"))


def projection_row(ordinal: int, row: dict[str, Any]) -> dict[str, Any]:
    target = row["_target"]
    alias = row["alias"]
    return {
        "alias_ordinal": ordinal,
        "alias_form_index": int(row["form_index"]),
        "alias_id": row["id"],
        "alias_encoding_name": row["encoding_name"],
        "target_form_index": int(target["form_index"]),
        "target_id": target["id"],
        "target_encoding_name": target["encoding_name"],
        "target_owner": owner_name(target),
        "alias_source_digest": row["source_digest"],
        "target_source_digest": target["source_digest"],
        "fixed_mask": row["fixed_mask"],
        "fixed_value": row["fixed_value"],
        "condition_program": list(alias.get("condition_program", [])),
        "preference_condition_program": list(alias.get("preference_condition_program", [])),
        "preference_rank": int(alias.get("preference_rank", 0)),
        "operand_count": len(row.get("operands", [])),
        "field_count": len(row.get("fields", [])),
    }


def c_string(value: str) -> str:
    return json.dumps(value, ensure_ascii=True)


def emit_header(rows: list[dict[str, Any]], output: Path) -> None:
    projections = [projection_row(index, row) for index, row in enumerate(rows)]
    executable_count = sum(generic_projection_executable(row) for row in rows)
    strings = bytearray()
    spans: dict[str, tuple[int, int]] = {}

    def span(value: str) -> tuple[int, int]:
        if value not in spans:
            offset = len(strings)
            encoded = value.encode("utf-8")
            strings.extend(encoded)
            strings.append(0)
            spans[value] = (offset, len(encoded))
        return spans[value]

    records: list[str] = []
    for row in projections:
        aid_off, aid_len = span(row["alias_id"])
        tid_off, tid_len = span(row["target_id"])
        records.append(
            "    { %du, %du, %du, %du, %du, UINT64_C(0x%016x), UINT64_C(0x%016x), UINT32_C(0x%08x), UINT32_C(0x%08x), %du, %du, %d, %du, %du, %du, %du, %du },"
            % (
                row["alias_ordinal"], row["alias_form_index"], row["target_form_index"],
                aid_off, tid_off,
                digest(row["alias_source_digest"]), digest(row["target_source_digest"]),
                int(row["fixed_mask"], 0), int(row["fixed_value"], 0),
                aid_len, tid_len, row["preference_rank"],
                {"memory": 0, "scalar_integer": 1, "direct_gpr": 2, "general_nonmemory": 3,
                 "system": 4, "complex_simd_fp": 5, "direct_simd": 6}.get(row["target_owner"], 255),
                row["operand_count"], row["field_count"], len(row["condition_program"]),
                len(row["preference_condition_program"]),
            )
        )
    pool = ",\n".join(", ".join(f"0x{byte:02x}" for byte in strings[index:index + 32]) for index in range(0, len(strings), 32))
    text = f"""/* Generated by generate_aarch64_alias_projection.py; do not edit. */
#ifndef BUSTER_AARCH64_ALIAS_PROJECTION_GENERATED_H
#define BUSTER_AARCH64_ALIAS_PROJECTION_GENERATED_H

#include <buster/lib/base.h>

#define BUSTER_A64_ALIAS_PROJECTION_SCHEMA_VERSION {SCHEMA_VERSION}u
#define BUSTER_A64_ALIAS_PROJECTION_FORM_COUNT {FORM_COUNT}u
#define BUSTER_A64_ALIAS_PROJECTION_CANONICAL_COUNT {CANONICAL_COUNT}u
#define BUSTER_A64_ALIAS_PROJECTION_ALIAS_COUNT {ALIAS_COUNT}u
#define BUSTER_A64_ALIAS_PROJECTION_MAX_OPERANDS {MAX_OPERANDS}u
#define BUSTER_A64_ALIAS_PROJECTION_MAX_FIELDS {MAX_FIELDS}u
#define BUSTER_A64_ALIAS_PROJECTION_MAX_CONDITION_TOKENS {MAX_CONDITION_TOKENS}u
#define BUSTER_A64_ALIAS_PROJECTION_GENERIC_EXECUTABLE_COUNT {executable_count}u
#define BUSTER_A64_ALIAS_PROJECTION_CANONICAL_SHA256 "{CANONICAL_SHA256}"
#define BUSTER_A64_ALIAS_PROJECTION_DENOMINATOR_SHA256 "{ALIAS_DENOMINATOR_SHA256}"
#define BUSTER_A64_ALIAS_PROJECTION_STRING_POOL_SIZE {len(strings)}u

typedef struct BusterA64AliasGeneratedRow BusterA64AliasGeneratedRow;
struct BusterA64AliasGeneratedRow
{{
    u32 alias_ordinal;
    u32 alias_form_index;
    u32 target_form_index;
    u32 alias_id_offset;
    u32 target_id_offset;
    u64 alias_source_digest;
    u64 target_source_digest;
    u32 fixed_mask;
    u32 fixed_value;
    u16 alias_id_length;
    u16 target_id_length;
    s32 preference_rank;
    u8 target_owner;
    u8 operand_count;
    u8 field_count;
    u8 condition_count;
    u8 preference_condition_count;
}};
BUSTER_CT_CHECK(sizeof(BusterA64AliasGeneratedRow) == 64);

static const char8 buster_a64_alias_projection_string_pool[] = {{
{pool}
}};

static const BusterA64AliasGeneratedRow buster_a64_alias_projection_rows[] = {{
{chr(10).join(records)}
}};

#endif
"""
    output.write_text(text)


def emit_outputs(canonical: list[dict[str, Any]], aliases: list[dict[str, Any]], output_header: Path,
                 output_jsonl: Path, output_manifest: Path) -> None:
    projections = [projection_row(index, row) for index, row in enumerate(aliases)]
    output_jsonl.write_text("".join(json.dumps(row, sort_keys=True, separators=(",", ":")) + "\n" for row in projections))
    emit_header(aliases, output_header)
    census: dict[str, int] = {}
    for row in projections:
        census[row["target_owner"]] = census.get(row["target_owner"], 0) + 1
    if census != OWNER_CENSUS:
        raise SystemExit(f"target-owner census changed: {census!r}")
    generic_executable_count = sum(generic_projection_executable(row) for row in aliases)
    manifest = {
        "schema_version": SCHEMA_VERSION,
        "form_count": FORM_COUNT,
        "canonical_count": CANONICAL_COUNT,
        "alias_count": ALIAS_COUNT,
        "canonical_sha256": CANONICAL_SHA256,
        "denominator_sha256": ALIAS_DENOMINATOR_SHA256,
        "target_owner_census": dict(sorted(census.items())),
        "max_operands": max(len(row.get("operands", [])) for row in aliases),
        "max_fields": max(len(row.get("fields", [])) for row in aliases),
        "max_condition_tokens": max(len(row["alias"].get("condition_program", [])) for row in aliases),
        "max_preference_condition_tokens": max(len(row["alias"].get("preference_condition_program", [])) for row in aliases),
        "nested_preferences": sum(bool(row["alias"].get("preferences")) for row in aliases),
        "generic_executable_count": generic_executable_count,
        "generic_unsupported_count": ALIAS_COUNT - generic_executable_count,
        "outputs": {
            "header": output_header.name,
            "jsonl": output_jsonl.name,
        },
    }
    output_manifest.write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", type=Path, required=True)
    parser.add_argument("--header", type=Path, required=True)
    parser.add_argument("--jsonl", type=Path, required=True)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--check", action="store_true", help="verify generated files are current")
    args = parser.parse_args()
    canonical, aliases, _ = load(args.input)
    if args.check:
        import tempfile
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            expected_header, expected_jsonl, expected_manifest = root / args.header.name, root / args.jsonl.name, root / args.manifest.name
            emit_outputs(canonical, aliases, expected_header, expected_jsonl, expected_manifest)
            for expected, actual in ((expected_header, args.header), (expected_jsonl, args.jsonl), (expected_manifest, args.manifest)):
                if expected.read_bytes() != actual.read_bytes():
                    raise SystemExit(f"generated output is stale: {actual}")
        return 0
    args.header.parent.mkdir(parents=True, exist_ok=True)
    args.jsonl.parent.mkdir(parents=True, exist_ok=True)
    args.manifest.parent.mkdir(parents=True, exist_ok=True)
    emit_outputs(canonical, aliases, args.header, args.jsonl, args.manifest)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
