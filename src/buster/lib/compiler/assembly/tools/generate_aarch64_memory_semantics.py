#!/usr/bin/env python3
"""Generate the checked-in Apple-M1 A64 memory semantic row projection.

The pinned semantic JSONL supplies the canonical row identity and transform
programs.  This generator independently checks the presentation projection
against the Arm XML: every source XML file and encoding exists, every canonical
field is an XML encoding box, and every semantic operand link is present in the
XML assembler template.  The generated C table is deliberately only a bounded
row index; all bit layout and transform data stays in the canonical semantic
and VM tables.
"""

from __future__ import annotations

import argparse
import collections
import hashlib
import json
import re
import xml.etree.ElementTree as ET
from pathlib import Path


CANONICAL_SHA256 = "8485c5c61835d5394d325757ab2964890e8bdfea304c6faa8fd4c23e4c7aabec"
EXPECTED_ROWS = 559
EXPECTED_DIGEST = "dc00ac602a16ecc51bbf5c2ba5d65dc5ae34704db9d9665467083b12bd94d842"
SCHEMA_VERSION = 1
ARRANGEMENT_SELECTOR_KINDS = {
    "simd_arrangement",
    "simd_width_selector",
    "simd_prefix_selector",
}
ARRANGEMENT_BINDING_NONE = 255
MAX_OPERANDS = 16


def denominator_digest(rows: list[dict]) -> str:
    ordered = sorted(rows, key=lambda row: row["id"])
    encoded = "".join(f"{row['id']}\t{row['source_digest']}\n" for row in ordered).encode()
    return hashlib.sha256(encoded).hexdigest()


def xml_text(element: ET.Element | None) -> str:
    return "".join(element.itertext()) if element is not None else ""


def validate_xml(row: dict, source: Path) -> dict[str, object]:
    provenance = row.get("provenance", {})
    xml_name = str(provenance.get("xml_file", ""))
    xml_path = source / xml_name
    if not xml_path.is_file():
        raise SystemExit(f"memory row {row['id']} references missing XML {xml_name}")
    root = ET.parse(xml_path).getroot()
    encodings = {element.get("name"): element for element in root.iter("encoding")}
    encoding_name = row["encoding_name"]
    encoding = encodings.get(encoding_name)
    if encoding is None:
        raise SystemExit(f"memory row {row['id']} has no XML encoding {encoding_name} in {xml_name}")
    # Arm places shared operand boxes on the enclosing iclass regdiagram and
    # fixed/differing boxes on the encoding itself.  Include both projections;
    # the canonical importer intentionally flattens them into one field list.
    boxes = {element.get("name") for element in encoding.iter("box") if element.get("name")}
    for diagram in root.iter("regdiagram"):
        boxes.update(element.get("name") for element in diagram.iter("box") if element.get("name"))
    missing_fields = [field["name"] for field in row.get("fields", []) if field["name"] not in boxes]
    if missing_fields:
        raise SystemExit(f"memory row {row['id']} fields missing from XML boxes: {missing_fields}")
    template = xml_text(encoding.find("asmtemplate"))
    links = {anchor.get("link") for anchor in encoding.iter("a") if anchor.get("link")}
    missing_links = []
    for operand in row.get("operands", []):
        link = operand.get("link", "")
        # XML anchors are the source of truth.  The semantic join may carry a
        # suffixed link, so accept only an exact link or the canonical base.
        base = re.sub(r"__\d+$", "", link)
        if link not in links and base not in {re.sub(r"__\d+$", "", value) for value in links}:
            missing_links.append(link)
    if missing_links:
        raise SystemExit(f"memory row {row['id']} links missing from XML: {missing_links}")
    return {"xml_file": xml_name, "encoding_name": encoding_name, "xml_box_count": len(boxes),
            "xml_link_count": len(links), "template_length": len(template)}


def family(row: dict) -> str:
    mnemonic = row["mnemonic"]
    if mnemonic in {"LD1", "LD2", "LD3", "LD4", "ST1", "ST2", "ST3", "ST4", "LD1R", "LD2R", "LD3R", "LD4R"}:
        return "simd_structure"
    if mnemonic.startswith(("LDADD", "LDCLR", "LDEOR", "LDSET", "LDSMAX", "LDSMIN", "LDUMAX", "LDUMIN", "SWP")) or mnemonic in {
        "CAS", "CASA", "CASAL", "CASL", "CASB", "CASH", "CASAB", "CASAH", "CASALB", "CASALH", "CASLB", "CASLH", "CASP", "CASPA", "CASPAL", "CASPL"
    }:
        return "atomic"
    if mnemonic.startswith(("LDXR", "LDAXR", "LDXP", "LDAXP", "STXR", "STLXR", "STXP", "STLXP")):
        return "exclusive"
    if mnemonic in {"LDAR", "LDARB", "LDARH", "LDAPR", "LDAPRB", "LDAPRH", "LDLAR", "LDLARB", "LDLARH", "STLR", "STLRB", "STLRH", "STLLR", "STLLRB", "STLLRH"}:
        return "ordered"
    if mnemonic in {"LDP", "LDNP", "LDPSW", "STP", "STNP"}:
        return "pair"
    return "scalar"


def address_mode(row: dict) -> str:
    assembly = row["assembly"]
    if "<index>" in assembly:
        return "simd_lane"
    if "]!" in assembly:
        return "pre_index"
    if re.search(r"\],\s*#", assembly):
        return "post_index"
    if "<Rm>" in assembly or "<Wm>" in assembly or "<Xm>" in assembly:
        return "register_offset"
    if "<pimm>" in assembly or "<imm12>" in assembly:
        return "scaled_offset"
    if "<simm>" in assembly or "<imm9>" in assembly or "<imm7>" in assembly or "<imm>" in assembly:
        return "signed_offset"
    return "base"


def overlap_policy(row: dict) -> str:
    family_name = family(row)
    if family_name == "atomic" and "CASP" in row["mnemonic"]:
        return "pair_disjoint"
    if family_name in {"exclusive", "pair"} or family_name == "simd_structure":
        return "adjacent_or_list"
    if any("writeback_" in flag for operand in row.get("operands", []) for flag in operand.get("flags", [])):
        return "base_disjoint"
    return "none"


def arrangement_bindings(row: dict) -> list[tuple[int, int]]:
    """Return syntax-directed register-to-selector bindings.

    Dynamic uppercase vector operands use the selector immediately following
    them (`<Vt>.<T>`); scalar `<T><d>` spellings use the preceding selector.
    List members are included as separate bindings because a structure form
    may switch from `Ta` to `Tb` between members.  This is deliberately
    structural rather than a nearest-distance heuristic.
    """
    operands = row.get("operands", [])
    selectors = {
        index
        for index, operand in enumerate(operands)
        if ARRANGEMENT_SELECTOR_KINDS.intersection(operand.get("kinds", []))
    }
    bindings = [(ARRANGEMENT_BINDING_NONE, 0) for _ in operands]
    for index, operand in enumerate(operands):
        kinds = set(operand.get("kinds", []))
        if "simd_register" not in kinds or ARRANGEMENT_SELECTOR_KINDS.intersection(kinds):
            continue
        symbol = operand.get("symbol", "").strip("<>")
        if symbol.startswith("V"):
            selector_index = index + 1
            if selector_index in selectors:
                bindings[index] = (selector_index, 1)
        elif symbol in {"d", "n", "m"}:
            selector_index = index - 1
            if selector_index in selectors:
                bindings[index] = (selector_index, -1)
    for index, (selector_index, direction) in enumerate(bindings):
        if selector_index == ARRANGEMENT_BINDING_NONE:
            continue
        if selector_index < 0 or selector_index >= len(operands) or selector_index not in selectors:
            raise SystemExit(f"invalid arrangement binding in {row['id']}: operand {index} -> {selector_index}")
        if direction == 1 and selector_index != index + 1:
            raise SystemExit(f"non-adjacent uppercase arrangement binding in {row['id']}")
        if direction == -1 and selector_index != index - 1:
            raise SystemExit(f"non-adjacent scalar arrangement binding in {row['id']}")
    return bindings


def lane_element_width(row: dict, operand_index: int) -> int:
    """Recover and validate the fixed element suffix for a lane index.

    Arm's lane index is a separate immediate operand, so its arrangement is
    carried by the exact `<Vt>.B[<index>]` (or H/S/D) syntax occurrence rather
    than by the immediate's encoding field.  Keep this check in generation so
    a changed XML template cannot silently fall back to an arbitrary width.
    """
    operands = row.get("operands", [])
    operand = operands[operand_index]
    symbol = operand.get("symbol", "")
    occurrence = sum(previous.get("symbol", "") == symbol for previous in operands[:operand_index])
    matches = [match.start() for match in re.finditer(re.escape(symbol), row["assembly"])]
    if occurrence >= len(matches):
        raise SystemExit(f"memory row {row['id']} lane symbol {symbol} missing from assembly")
    offset = matches[occurrence]
    for back in range(offset - 1, max(-1, offset - 20), -1):
        character = row["assembly"][back]
        if character in "Bb":
            return 8
        if character in "Hh":
            return 16
        if character in "Ss":
            return 32
        if character in "Dd":
            return 64
        if character == ",":
            break
    raise SystemExit(f"memory row {row['id']} lane symbol {symbol} has no B/H/S/D suffix")


def lane_element_widths(row: dict) -> list[dict[str, int]]:
    result = []
    for index, operand in enumerate(row.get("operands", [])):
        if "simd_lane" in operand.get("kinds", []) or "simd_lane_index" in operand.get("flags", []):
            width = lane_element_width(row, index)
            if width not in {8, 16, 32, 64}:
                raise SystemExit(f"memory row {row['id']} has invalid lane element width {width}")
            result.append({"operand_index": index, "element_width": width})
    return result


def load_rows(path: Path, source: Path) -> tuple[list[dict], dict[str, object]]:
    rows = [json.loads(line) for line in path.read_text().splitlines() if line.strip()]
    memory = [row for row in rows if row.get("owner") == "memory" and row.get("kind") == "canonical"]
    memory.sort(key=lambda row: int(row["form_index"]))
    if len(memory) != EXPECTED_ROWS:
        raise SystemExit(f"memory denominator changed: {len(memory)} != {EXPECTED_ROWS}")
    if denominator_digest(memory) != EXPECTED_DIGEST:
        raise SystemExit("memory denominator digest changed")
    if any(row.get("status") != "defined" for row in memory):
        raise SystemExit("memory denominator contains an undefined row")
    if any(row.get("alias", {}).get("kind") != "canonical" for row in memory):
        raise SystemExit("memory denominator contains an alias row")
    if any(not row.get("raw_layout_resolved", False) for row in memory):
        raise SystemExit("memory denominator contains an unresolved raw layout")
    xml_checks = [validate_xml(row, source) for row in memory]
    for row in memory:
        arrangement_bindings(row)
        lane_element_widths(row)
    census = collections.Counter(family(row) for row in memory)
    modes = collections.Counter(address_mode(row) for row in memory)
    overlaps = collections.Counter(overlap_policy(row) for row in memory)
    feature_rows = sum(bool(row.get("constraints", {}).get("feature_tags")) for row in memory)
    transform_rows = sum(bool(row.get("transforms")) for row in memory)
    arrangement_binding_count = sum(sum(selector != ARRANGEMENT_BINDING_NONE for selector, _direction in arrangement_bindings(row))
                                    for row in memory)
    literal_control = ["0x5c000000", "0x9c000000", "0x1c000000"]
    audit = {
        "canonical_sha256": CANONICAL_SHA256,
        "denominator_sha256": EXPECTED_DIGEST,
        "row_count": len(memory),
        "transform_row_count": transform_rows,
        "feature_gated_row_count": feature_rows,
        "arrangement_binding_count": arrangement_binding_count,
        "family_counts": dict(sorted(census.items())),
        "address_mode_counts": dict(sorted(modes.items())),
        "overlap_policy_counts": dict(sorted(overlaps.items())),
        "literal_control_overlap_rows": literal_control,
        "xml_validation": {
            "row_count": len(xml_checks),
            "source_files": len({check["xml_file"] for check in xml_checks}),
            "all_encoding_names_present": True,
            "all_fields_present_as_boxes": True,
            "all_operand_links_present_in_templates": True,
        },
    }
    return memory, audit


def emit_header(rows: list[dict], audit: dict[str, object], output: Path) -> None:
    lines = [
        "/* Generated by generate_aarch64_memory_semantics.py; do not edit. */",
        "#ifndef BUSTER_AARCH64_MEMORY_SEMANTICS_GENERATED_H",
        "#define BUSTER_AARCH64_MEMORY_SEMANTICS_GENERATED_H",
        "",
        "#include <buster/lib/base.h>",
        "",
        f"#define BUSTER_A64_MEMORY_SCHEMA_VERSION {SCHEMA_VERSION}u",
        f"#define BUSTER_A64_MEMORY_ROW_COUNT {len(rows)}u",
        f"#define BUSTER_A64_MEMORY_TRANSFORM_ROW_COUNT {audit['transform_row_count']}u",
        f"#define BUSTER_A64_MEMORY_FEATURE_GATED_ROW_COUNT {audit['feature_gated_row_count']}u",
        f"#define BUSTER_A64_MEMORY_ARRANGEMENT_BINDING_COUNT {audit['arrangement_binding_count']}u",
        f"#define BUSTER_A64_MEMORY_DENOMINATOR_SHA256 \"{EXPECTED_DIGEST}\"",
        f"#define BUSTER_A64_MEMORY_CANONICAL_SHA256 \"{CANONICAL_SHA256}\"",
        "#define BUSTER_A64_MEMORY_LITERAL_CONTROL_OVERLAP_COUNT 3u",
        f"#define BUSTER_A64_MEMORY_GENERATED_MAX_OPERANDS {MAX_OPERANDS}u",
        f"#define BUSTER_A64_MEMORY_ARRANGEMENT_BINDING_NONE {ARRANGEMENT_BINDING_NONE}u",
        "",
        "typedef struct BusterA64MemoryGeneratedRow BusterA64MemoryGeneratedRow;",
        "struct BusterA64MemoryGeneratedRow",
        "{",
        "    u32 row_index;",
        "    u32 semantic_form_id;",
        "    u64 source_digest;",
        "    u8 operand_count;",
        "    u8 family;",
        "    u8 address_mode;",
        "    u8 overlap_policy;",
        "    u8 candidate;",
        "};",
        "BUSTER_CT_CHECK(sizeof(BusterA64MemoryGeneratedRow) == 24);",
        "",
        "typedef struct BusterA64MemoryGeneratedArrangementBinding BusterA64MemoryGeneratedArrangementBinding;",
        "struct BusterA64MemoryGeneratedArrangementBinding",
        "{",
        "    u8 selector_index;",
        "    s8 direction;",
        "};",
        "BUSTER_CT_CHECK(sizeof(BusterA64MemoryGeneratedArrangementBinding) == 2);",
        "",
        "static const u32 buster_a64_memory_literal_control_overlap_words[3] = { UINT32_C(0x5c000000), UINT32_C(0x9c000000), UINT32_C(0x1c000000) };",
        "",
        "static const BusterA64MemoryGeneratedRow buster_a64_memory_generated_rows[] = {",
    ]
    family_values = {name: index for index, name in enumerate(("scalar", "pair", "exclusive", "ordered", "atomic", "simd_structure"))}
    mode_values = {name: index for index, name in enumerate(("base", "signed_offset", "scaled_offset", "register_offset", "pre_index", "post_index", "simd_lane"))}
    overlap_values = {name: index for index, name in enumerate(("none", "base_disjoint", "adjacent_or_list", "pair_disjoint"))}
    binding_rows: list[str] = []
    for index, row in enumerate(rows):
        lines.append("    { %du, %du, UINT64_C(0x%016x), %du, %du, %du, %du, 1u }," % (
            index, int(row["form_index"]), int(row["source_digest"], 0), len(row.get("operands", [])),
            family_values[family(row)], mode_values[address_mode(row)], overlap_values[overlap_policy(row)]))
        row_bindings = arrangement_bindings(row)
        row_bindings += [(ARRANGEMENT_BINDING_NONE, 0)] * (MAX_OPERANDS - len(row_bindings))
        binding_rows.append("    { " + ", ".join("{ %du, %d }" % pair for pair in row_bindings) + " },")
    lines.extend(["};", "", "static const BusterA64MemoryGeneratedArrangementBinding",
                  "    buster_a64_memory_generated_arrangement_bindings[BUSTER_A64_MEMORY_ROW_COUNT][BUSTER_A64_MEMORY_GENERATED_MAX_OPERANDS] = {",
                  *binding_rows, "};", "", "#endif", ""])
    output.write_text("\n".join(lines))


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", type=Path, default=Path("src/buster/lib/compiler/assembly/generated/arm-a64-semantic.generated.jsonl"))
    parser.add_argument("--source", type=Path, default=Path("work/sources/arm-a64-2026-06"))
    parser.add_argument("--header", type=Path, default=Path("src/buster/lib/compiler/assembly/generated/aarch64-memory-semantics.generated.h"))
    parser.add_argument("--manifest", type=Path, default=Path("src/buster/lib/compiler/assembly/generated/aarch64-memory-semantics.manifest.json"))
    args = parser.parse_args()
    rows, audit = load_rows(args.input, args.source)
    args.header.parent.mkdir(parents=True, exist_ok=True)
    emit_header(rows, audit, args.header)
    manifest = {"schema_version": SCHEMA_VERSION, "owner": "memory", "target": "apple-m1", **audit,
                "rows": [{"row_index": index, "semantic_form_id": int(row["form_index"]), "id": row["id"],
                           "source_digest": row["source_digest"], "family": family(row), "address_mode": address_mode(row),
                           "overlap_policy": overlap_policy(row), "operand_count": len(row.get("operands", [])),
                           "transform_bearing": bool(row.get("transforms")),
                           "arrangement_bindings": [{"selector_index": selector, "direction": direction}
                                                    for selector, direction in arrangement_bindings(row)
                                                    if selector != ARRANGEMENT_BINDING_NONE],
                           "lane_element_widths": lane_element_widths(row),
                           "status": "candidate"}
                          for index, row in enumerate(rows)]}
    args.manifest.write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n")


if __name__ == "__main__":
    main()
