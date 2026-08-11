#!/usr/bin/env python3
"""Generate the bounded Apple-M1 direct-AdvSIMD row catalog.

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


EXPECTED_ROWS = 390
EXPECTED_TRANSFORM_ROWS = 263
EXPECTED_MAX_OPERANDS = 8
EXPECTED_DENOMINATOR = "e4fb3407ffefd2e592a09471958a5996139bc0e966b5043e4d48e3ebe7d0a805"
CANONICAL_SHA256 = "8485c5c61835d5394d325757ab2964890e8bdfea304c6faa8fd4c23e4c7aabec"
EXPLICIT_GAP_NAMES = {
    "BIC_asimdimm_L_hl",
    "BIC_asimdimm_L_sl",
    "MOVI_asimdimm_L_hl",
    "MOVI_asimdimm_L_sl",
    "MOVI_asimdimm_M_sm",
    "MVNI_asimdimm_L_hl",
    "MVNI_asimdimm_L_sl",
    "MVNI_asimdimm_M_sm",
}
TBL_TBX_METADATA_CORRECTION_FORMS = [1566, 1567, 1568, 1569, 1571, 1572, 1573, 1574]
ARRANGEMENT_SELECTOR_KINDS = {
    "simd_arrangement",
    "simd_width_selector",
    "simd_prefix_selector",
}
ARRANGEMENT_BINDING_NONE = 255


def digest(value: str) -> int:
    return int(value, 0)


def load_rows(path: Path) -> tuple[list[dict], list[dict]]:
    rows = [json.loads(line) for line in path.read_text().splitlines() if line.strip()]
    direct = [row for row in rows if row.get("owner") == "direct_simd" and row.get("kind") == "canonical"]
    gaps = [row for row in rows if row.get("encoding_name") in EXPLICIT_GAP_NAMES and row.get("kind") == "canonical"]
    direct.sort(key=lambda row: int(row["form_index"]))
    gaps.sort(key=lambda row: int(row["form_index"]))
    if len(direct) != EXPECTED_ROWS:
        raise SystemExit(f"direct_simd denominator changed: {len(direct)} != {EXPECTED_ROWS}")
    if len({int(row["form_index"]) for row in direct}) != EXPECTED_ROWS:
        raise SystemExit("direct_simd semantic form IDs are not unique")
    if sum(bool(row.get("transforms")) for row in direct) != EXPECTED_TRANSFORM_ROWS:
        raise SystemExit("direct_simd transform-bearing count changed")
    if max(len(row.get("operands", [])) for row in direct) != EXPECTED_MAX_OPERANDS:
        raise SystemExit("direct_simd maximum operand count changed")
    if any(row.get("alias", {}).get("kind") != "canonical" for row in direct):
        raise SystemExit("direct_simd contains an alias row")
    if len(gaps) != len(EXPLICIT_GAP_NAMES):
        raise SystemExit(f"explicit cmode gap census changed: {len(gaps)} != {len(EXPLICIT_GAP_NAMES)}")
    if any(row.get("owner") == "direct_simd" for row in gaps):
        raise SystemExit("explicit gap unexpectedly belongs to direct_simd")
    return direct, gaps


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


def emit_header(rows: list[dict], gaps: list[dict], output: Path) -> None:
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
    text = """/* Generated by generate_aarch64_direct_simd.py; do not edit. */
#ifndef BUSTER_AARCH64_DIRECT_SIMD_GENERATED_H
#define BUSTER_AARCH64_DIRECT_SIMD_GENERATED_H

#include <buster/lib/base.h>

#define BUSTER_A64_DIRECT_SIMD_SCHEMA_VERSION 1u
#define BUSTER_A64_DIRECT_SIMD_ROW_COUNT 390u
#define BUSTER_A64_DIRECT_SIMD_TRANSFORM_ROW_COUNT 263u
#define BUSTER_A64_DIRECT_SIMD_MAX_OPERANDS 8u
#define BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_BINDING_NONE 255u
#define BUSTER_A64_DIRECT_SIMD_ARRANGEMENT_BINDING_COUNT %du
#define BUSTER_A64_DIRECT_SIMD_EXECUTABLE_ROW_COUNT 390u
#define BUSTER_A64_DIRECT_SIMD_CROSS_OWNER_GAP_COUNT %du
#define BUSTER_A64_DIRECT_SIMD_DENOMINATOR_SHA256 "e4fb3407ffefd2e592a09471958a5996139bc0e966b5043e4d48e3ebe7d0a805"
#define BUSTER_A64_DIRECT_SIMD_CANONICAL_SHA256 "8485c5c61835d5394d325757ab2964890e8bdfea304c6faa8fd4c23e4c7aabec"
#define BUSTER_A64_DIRECT_SIMD_STRING_POOL_SIZE %du

typedef struct BusterA64DirectSIMDGeneratedRow BusterA64DirectSIMDGeneratedRow;
struct BusterA64DirectSIMDGeneratedRow
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
BUSTER_CT_CHECK(sizeof(BusterA64DirectSIMDGeneratedRow) == 32);

typedef struct BusterA64DirectSIMDGeneratedArrangementBinding BusterA64DirectSIMDGeneratedArrangementBinding;
struct BusterA64DirectSIMDGeneratedArrangementBinding
{
    u8 selector_index;
    s8 direction;
};
BUSTER_CT_CHECK(sizeof(BusterA64DirectSIMDGeneratedArrangementBinding) == 2);

static const char8 buster_a64_direct_simd_generated_string_pool[] = {
""" % (arrangement_binding_count, len(gaps), len(strings)) + pool + """
};

static const BusterA64DirectSIMDGeneratedRow buster_a64_direct_simd_generated_rows[] = {
""" + "\n".join(row_lines) + """
};

static const BusterA64DirectSIMDGeneratedArrangementBinding
    buster_a64_direct_simd_generated_arrangement_bindings[BUSTER_A64_DIRECT_SIMD_ROW_COUNT][BUSTER_A64_DIRECT_SIMD_MAX_OPERANDS] = {
""" + "\n".join(arrangement_binding_rows) + """
};

#endif
"""
    output.write_text(text)


def emit_manifest(rows: list[dict], gaps: list[dict], output: Path) -> None:
    computed = denominator_digest(rows)
    if computed != EXPECTED_DENOMINATOR:
        raise SystemExit(f"direct_simd denominator digest changed: {computed} != {EXPECTED_DENOMINATOR}")
    manifest = {
        "schema_version": 1,
        "owner": "direct_simd",
        "target": "apple-m1",
        "canonical_sha256": CANONICAL_SHA256,
        "denominator_sha256": EXPECTED_DENOMINATOR,
        "denominator_sha256_computed": computed,
        "row_count": len(rows),
        "transform_row_count": sum(bool(row.get("transforms")) for row in rows),
        "executable_row_count": len(rows),
        "cross_owner_gap_count": len(gaps),
        "max_operands": max(len(row.get("operands", [])) for row in rows),
        "arrangement_binding_count": sum(
            selector_index != ARRANGEMENT_BINDING_NONE
            for row in rows
            for selector_index, _direction in arrangement_bindings(row)
        ),
        "metadata_corrections": [
            {
                "family": "TBL/TBX",
                "semantic_form_ids": TBL_TBX_METADATA_CORRECTION_FORMS,
                "operand_presentation_kind": "simd_lane",
                "typed_kind": "simd_vector",
                "reason": "Vm.<Ta> is an index vector; the encoding has no numeric element-lane field",
            }
        ],
        "cross_owner_gaps": [
            {
                "semantic_form_id": int(row["form_index"]),
                "encoding_name": row["encoding_name"],
                "id": row["id"],
                "source_digest": row["source_digest"],
                "status": "cross_owner_gap",
                "reason": "cmode-dependent AdvSIMD immediate transform is owned by complex_simd_fp, outside the direct_simd denominator",
            }
            for row in gaps
        ],
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
    parser.add_argument("--header", type=Path, default=Path("src/buster/lib/compiler/assembly/generated/aarch64-direct-simd.generated.h"))
    parser.add_argument("--manifest", type=Path, default=Path("src/buster/lib/compiler/assembly/generated/aarch64-direct-simd.manifest.json"))
    args = parser.parse_args()
    rows, gaps = load_rows(args.input)
    args.header.parent.mkdir(parents=True, exist_ok=True)
    emit_header(rows, gaps, args.header)
    emit_manifest(rows, gaps, args.manifest)


if __name__ == "__main__":
    main()
