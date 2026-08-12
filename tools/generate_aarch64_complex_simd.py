#!/usr/bin/env python3
"""Generate the bounded Apple-M1 complex AdvSIMD/FP row catalog.

The semantic JSONL remains the source of truth.  This projection intentionally
contains only the stable row identity and bounded presentation fields needed by
the direct SIMD runtime; all bit layout and transform data is obtained through
the typed semantic/VM accessors at runtime.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path


EXPECTED_ROWS = 345
EXPECTED_TRANSFORM_ROWS = 206
EXPECTED_MAX_OPERANDS = 8
EXPECTED_DENOMINATOR = "b186dfa380b7067e23f007df9d5b78d0a21130e89886e6c1ede4379885fed0c5"
CANONICAL_SHA256 = "8485c5c61835d5394d325757ab2964890e8bdfea304c6faa8fd4c23e4c7aabec"
LITERAL_CONTROL_OVERLAP_NAMES = {"LDR_D_loadlit", "LDR_Q_loadlit", "LDR_S_loadlit"}
CMODE_LOGICAL_SLICE_NAMES = {
    "BIC_asimdimm_L_hl", "BIC_asimdimm_L_sl", "MOVI_asimdimm_L_hl", "MOVI_asimdimm_L_sl",
    "MOVI_asimdimm_M_sm", "MVNI_asimdimm_L_hl", "MVNI_asimdimm_L_sl", "MVNI_asimdimm_M_sm",
}
ARRANGEMENT_SELECTOR_KINDS = {
    "simd_arrangement",
    "simd_width_selector",
    "simd_prefix_selector",
}
ARRANGEMENT_BINDING_NONE = 255


def digest(value: str) -> int:
    return int(value, 0)


def load_rows(path: Path) -> tuple[list[dict], dict[str, object]]:
    rows = [json.loads(line) for line in path.read_text().splitlines() if line.strip()]
    complex_rows = [row for row in rows if row.get("owner") == "complex_simd_fp" and row.get("kind") == "canonical" and
                    row.get("encoding_name") not in LITERAL_CONTROL_OVERLAP_NAMES]
    complex_rows.sort(key=lambda row: int(row["form_index"]))
    if len(complex_rows) != EXPECTED_ROWS:
        raise SystemExit(f"complex_simd denominator changed: {len(complex_rows)} != {EXPECTED_ROWS}")
    if len({int(row["form_index"]) for row in complex_rows}) != EXPECTED_ROWS:
        raise SystemExit("complex_simd semantic form IDs are not unique")
    if sum(bool(row.get("transforms")) for row in complex_rows) != EXPECTED_TRANSFORM_ROWS:
        raise SystemExit("complex_simd transform-bearing count changed")
    if max(len(row.get("operands", [])) for row in complex_rows) != EXPECTED_MAX_OPERANDS:
        raise SystemExit("complex_simd maximum operand count changed")
    if any(row.get("alias", {}).get("kind") != "canonical" for row in complex_rows):
        raise SystemExit("complex_simd contains an alias row")
    if any(row.get("status") != "defined" or not row.get("raw_layout_resolved", False) for row in complex_rows):
        raise SystemExit("complex_simd denominator contains an undefined or unresolved row")
    overlaps = [row for row in rows if row.get("encoding_name") in LITERAL_CONTROL_OVERLAP_NAMES and row.get("kind") == "canonical"]
    if len(overlaps) != 3:
        raise SystemExit(f"literal-control overlap census changed: {len(overlaps)} != 3")
    taxonomy = {
        "literal_control_overlap_rows": [row["id"] for row in sorted(overlaps, key=lambda row: row["id"])],
        "cmode_logical_slice_rows": [row["id"] for row in complex_rows if row.get("encoding_name") in CMODE_LOGICAL_SLICE_NAMES],
        "feature_gated_row_count": sum(bool(row.get("constraints", {}).get("feature_tags")) for row in complex_rows),
        "operand_kind_counts": {},
        "transform_kind_counts": {},
    }
    operand_counts: dict[str, int] = {}
    transform_counts: dict[str, int] = {}
    for row in complex_rows:
        for operand in row.get("operands", []):
            for kind in operand.get("kinds", []): operand_counts[kind] = operand_counts.get(kind, 0) + 1
        for transform in row.get("transforms", []):
            kind = transform.get("kind", "unknown")
            transform_counts[kind] = transform_counts.get(kind, 0) + 1
    taxonomy["operand_kind_counts"] = dict(sorted(operand_counts.items()))
    taxonomy["transform_kind_counts"] = dict(sorted(transform_counts.items()))
    return complex_rows, taxonomy


def denominator_digest(rows: list[dict]) -> str:
    # The denominator identity is defined over the canonical source digest
    # sequence, not over JSON formatting or host integer representation.
    ordered = sorted(rows, key=lambda row: row["id"])
    encoded = "".join(f"{row['id']}\t{row['source_digest']}\n" for row in ordered).encode()
    return hashlib.sha256(encoded).hexdigest()


def c_string(value: str) -> str:
    return json.dumps(value, ensure_ascii=True)


def arrangement_bindings(row: dict) -> list[tuple[int, int]]:
    """Return the syntax-directed register-to-selector bindings for a row.

    The semantic operands preserve the presentation syntax, so a dynamic
    uppercase vector register is followed by its arrangement anchor while a
    dynamic lowercase scalar register is preceded by its width anchor.  The
    generated relation is deliberately structural: choosing a selector by
    nearest distance is incorrect for mixed Ta/Tb forms.  Combined operands
    such as ``<Va>`` that are themselves arrangement selectors and fixed list
    members are intentionally left unbound.
    """
    operands = row.get("operands", [])
    selector_indices = {
        index
        for index, operand in enumerate(operands)
        if ARRANGEMENT_SELECTOR_KINDS.intersection(operand.get("kinds", []))
    }
    bindings = [(ARRANGEMENT_BINDING_NONE, 0) for _ in operands]
    for index, operand in enumerate(operands):
        kinds = set(operand.get("kinds", []))
        flags = set(operand.get("flags", []))
        if "simd_register" not in kinds or ARRANGEMENT_SELECTOR_KINDS.intersection(kinds) or "simd_list_member" in flags:
            continue
        symbol = operand.get("symbol", "").strip("<>")
        if symbol.startswith("V"):
            selector_index = index + 1
            if selector_index not in selector_indices:
                # Fixed-suffix registers (for example AES and TBL list
                # members) obtain their arrangement from assembly syntax.
                continue
            bindings[index] = (selector_index, 1)
        elif symbol in {"d", "n", "m"}:
            selector_index = index - 1
            if selector_index not in selector_indices:
                continue
            bindings[index] = (selector_index, -1)

    # Every dynamic operand that has an arrangement selector must be directly
    # adjacent to its anchor.  Keep this invariant in the generator so the
    # runtime never has to recover ambiguous syntax with a heuristic.
    for index, (selector_index, direction) in enumerate(bindings):
        if selector_index == ARRANGEMENT_BINDING_NONE:
            continue
        if selector_index < 0 or selector_index >= len(operands) or selector_index not in selector_indices:
            raise SystemExit(f"invalid arrangement binding in {row['id']}: operand {index} -> {selector_index}")
        if direction == 1 and selector_index != index + 1:
            raise SystemExit(f"non-adjacent uppercase arrangement binding in {row['id']}")
        if direction == -1 and selector_index != index - 1:
            raise SystemExit(f"non-adjacent scalar arrangement binding in {row['id']}")
        if direction not in {-1, 1}:
            raise SystemExit(f"invalid arrangement binding direction in {row['id']}")
    return bindings


def emit_header(rows: list[dict], taxonomy: dict[str, object], output: Path) -> None:
    strings = bytearray()
    spans: dict[str, tuple[int, int]] = {}

    def span(value: str) -> tuple[int, int]:
        if value not in spans:
            offset = len(strings)
            strings.extend(value.encode("utf-8"))
            spans[value] = (offset, len(value.encode("utf-8")))
        return spans[value]

    row_lines: list[str] = []
    arrangement_binding_rows: list[str] = []
    arrangement_binding_count = 0
    for index, row in enumerate(rows):
        row_id_offset, row_id_length = span(row["id"])
        assembly_offset, assembly_length = span(row["assembly"])
        row_lines.append(
            "    { %du, %du, UINT64_C(0x%016x), %du, %du, %du, %du, %du, %du },"
            % (
                index,
                int(row["form_index"]),
                digest(row["source_digest"]),
                row_id_offset,
                row_id_length,
                assembly_offset,
                assembly_length,
                len(row.get("operands", [])),
                1,
            )
        )
        bindings = arrangement_bindings(row)
        arrangement_binding_count += sum(selector_index != ARRANGEMENT_BINDING_NONE for selector_index, _direction in bindings)
        binding_values = bindings + [(ARRANGEMENT_BINDING_NONE, 0)] * (EXPECTED_MAX_OPERANDS - len(bindings))
        arrangement_binding_rows.append(
            "    { "
            + ", ".join("{ %du, %d }" % (selector_index, direction) for selector_index, direction in binding_values)
            + " },"
        )

    pool_parts = [", ".join(f"0x{byte:02x}" for byte in strings[index : index + 32]) for index in range(0, len(strings), 32)]
    pool = ",\n".join(pool_parts)
    text = """/* Generated by generate_aarch64_complex_simd.py; do not edit. */
#ifndef BUSTER_AARCH64_COMPLEX_SIMD_GENERATED_H
#define BUSTER_AARCH64_COMPLEX_SIMD_GENERATED_H

#include <buster/lib/base.h>

#define BUSTER_A64_COMPLEX_SIMD_SCHEMA_VERSION 1u
#define BUSTER_A64_COMPLEX_SIMD_ROW_COUNT 345u
#define BUSTER_A64_COMPLEX_SIMD_TRANSFORM_ROW_COUNT 206u
#define BUSTER_A64_COMPLEX_SIMD_MAX_OPERANDS 8u
#define BUSTER_A64_COMPLEX_SIMD_ARRANGEMENT_BINDING_NONE 255u
#define BUSTER_A64_COMPLEX_SIMD_ARRANGEMENT_BINDING_COUNT %du
#define BUSTER_A64_COMPLEX_SIMD_EXECUTABLE_ROW_COUNT 345u
#define BUSTER_A64_COMPLEX_SIMD_FEATURE_GATED_ROW_COUNT %du
#define BUSTER_A64_COMPLEX_SIMD_CMODE_LOGICAL_SLICE_COUNT 8u
#define BUSTER_A64_COMPLEX_SIMD_LITERAL_CONTROL_OVERLAP_COUNT 3u
#define BUSTER_A64_COMPLEX_SIMD_DENOMINATOR_SHA256 "b186dfa380b7067e23f007df9d5b78d0a21130e89886e6c1ede4379885fed0c5"
#define BUSTER_A64_COMPLEX_SIMD_CANONICAL_SHA256 "8485c5c61835d5394d325757ab2964890e8bdfea304c6faa8fd4c23e4c7aabec"
#define BUSTER_A64_COMPLEX_SIMD_STRING_POOL_SIZE %du

typedef struct BusterA64ComplexSIMDGeneratedRow BusterA64ComplexSIMDGeneratedRow;
struct BusterA64ComplexSIMDGeneratedRow
{
    u32 row_index;
    u32 semantic_form_id;
    u64 source_digest;
    u32 id_offset;
    u16 id_length;
    u32 assembly_offset;
    u16 assembly_length;
    u8 operand_count;
    u8 executable;
};
BUSTER_CT_CHECK(sizeof(BusterA64ComplexSIMDGeneratedRow) == 32);

typedef struct BusterA64ComplexSIMDGeneratedArrangementBinding BusterA64ComplexSIMDGeneratedArrangementBinding;
struct BusterA64ComplexSIMDGeneratedArrangementBinding
{
    u8 selector_index;
    s8 direction;
};
BUSTER_CT_CHECK(sizeof(BusterA64ComplexSIMDGeneratedArrangementBinding) == 2);

static const char8 buster_a64_complex_simd_generated_string_pool[] = {
""" % (arrangement_binding_count, int(taxonomy["feature_gated_row_count"]), len(strings)) + pool + """
};

static const BusterA64ComplexSIMDGeneratedRow buster_a64_complex_simd_generated_rows[] = {
""" + "\n".join(row_lines) + """
};

static const BusterA64ComplexSIMDGeneratedArrangementBinding
    buster_a64_complex_simd_generated_arrangement_bindings[BUSTER_A64_COMPLEX_SIMD_ROW_COUNT][BUSTER_A64_COMPLEX_SIMD_MAX_OPERANDS] = {
""" + "\n".join(arrangement_binding_rows) + """
};

#endif
"""
    output.write_text(text)


def emit_manifest(rows: list[dict], taxonomy: dict[str, object], output: Path) -> None:
    computed = denominator_digest(rows)
    if computed != EXPECTED_DENOMINATOR:
        raise SystemExit(f"complex_simd denominator digest changed: {computed} != {EXPECTED_DENOMINATOR}")
    manifest = {
        "schema_version": 1,
        "owner": "complex_simd_fp",
        "target": "apple-m1",
        "canonical_sha256": CANONICAL_SHA256,
        "denominator_sha256": EXPECTED_DENOMINATOR,
        "denominator_sha256_computed": computed,
        "row_count": len(rows),
        "transform_row_count": sum(bool(row.get("transforms")) for row in rows),
        "executable_row_count": len(rows),
        "feature_gated_row_count": taxonomy["feature_gated_row_count"],
        "max_operands": max(len(row.get("operands", [])) for row in rows),
        "arrangement_binding_count": sum(
            selector_index != ARRANGEMENT_BINDING_NONE
            for row in rows
            for selector_index, _direction in arrangement_bindings(row)
        ),
        "literal_control_overlap_rows": taxonomy["literal_control_overlap_rows"],
        "cmode_logical_slice_rows": taxonomy["cmode_logical_slice_rows"],
        "operand_kind_counts": taxonomy["operand_kind_counts"],
        "transform_kind_counts": taxonomy["transform_kind_counts"],
        "rows": [
            {
                "row_index": index,
                "semantic_form_id": int(row["form_index"]),
                "id": row["id"],
                "source_digest": row["source_digest"],
                "status": "executable",
                "transform_bearing": bool(row.get("transforms")),
                "arrangement_bindings": [
                    {
                        "operand_index": operand_index,
                        "selector_index": selector_index,
                        "direction": direction,
                    }
                    for operand_index, (selector_index, direction) in enumerate(arrangement_bindings(row))
                    if selector_index != ARRANGEMENT_BINDING_NONE
                ],
            }
            for index, row in enumerate(rows)
        ],
    }
    output.write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", type=Path, default=Path("src/buster/lib/compiler/assembly/generated/arm-a64-semantic.generated.jsonl"))
    parser.add_argument("--header", type=Path, default=Path("src/buster/lib/compiler/assembly/generated/aarch64-complex-simd.generated.h"))
    parser.add_argument("--manifest", type=Path, default=Path("src/buster/lib/compiler/assembly/generated/aarch64-complex-simd.manifest.json"))
    args = parser.parse_args()
    rows, taxonomy = load_rows(args.input)
    args.header.parent.mkdir(parents=True, exist_ok=True)
    emit_header(rows, taxonomy, args.header)
    emit_manifest(rows, taxonomy, args.manifest)


if __name__ == "__main__":
    main()
