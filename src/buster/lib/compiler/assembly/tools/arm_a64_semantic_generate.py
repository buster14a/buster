#!/usr/bin/env python3
"""Generate the bounded Arm A64 semantic-binding snapshot.

The canonical Arm importer intentionally keeps only encoding/layout facts.  A
semantic consumer also needs the XML syntax anchors and explanations.  This
small, deterministic compiler joins those two namespaces by XML link and
enclist (never by display spelling), then emits a JSONL audit projection and a
pointer-free C table.  It is deliberately dependency-free and is usable both
from a checkout and from a release source archive.
"""

from __future__ import annotations

import argparse
import base64
import collections
import hashlib
import json
import re
import struct
import sys
import xml.etree.ElementTree as ET
from pathlib import Path
from typing import Any, Iterable


CANONICAL_SHA256 = "8485c5c61835d5394d325757ab2964890e8bdfea304c6faa8fd4c23e4c7aabec"
CANONICAL_COUNT = 1695
CANONICAL_DENOMINATOR = 1523
ALIAS_DENOMINATOR = 172
SOURCE_TREE_SHA256 = "0ee17fd2fe7ed165adda377d90f8f284d009e14d2300577f231c87ca6a45916d"
# Version 2 adds typed VM program instructions and bounded program spans for
# both transform programs and value-table PROGRAM atoms.  Consumers must not
# treat a version-1 artifact (which only carried diagnostic JSON text) as
# equivalent to this shape.
SCHEMA_VERSION = 2

PROGRAM_OP_VALUES = {
    "field": 0,
    "uint_concat": 1,
    "sign_extend": 2,
    "scale_mul": 3,
    "scale_div": 4,
    "scale_pow2": 5,
    "add_const": 6,
    "sub_from_const": 7,
    "register_add_mod": 8,
    "literal": 9,
    "text_factor": 10,
    "shared_decode": 11,
}
PROGRAM_OPERAND_KIND_VALUES = {"field": 0, "arrangement": 1, "literal": 2}

# Operand flags are a public, snapshot-stable bitset.  Keep this list sorted
# and explicit: assigning bits from the per-operand ``set`` order would make
# the generated ABI change when XML anchors are reordered.
SEMANTIC_FLAG_NAMES = (
    "arrangement_selector", "barrier", "bitmask_transform", "branch_rel14",
    "branch_rel19", "branch_rel26", "condition_field", "condition_inverted",
    "default_zero", "extend_option", "fixed_literal_2", "fp_imm8_encoding",
    "fp_or_fixed_point", "gpr_width_w32", "gpr_width_x64", "index_immediate",
    "memory_base", "memory_offset", "memory_writeback", "nzcv_4bit", "optional",
    "optional_defaults_x30", "page_relative", "pc_relative", "pc_relative_adr",
    "prefetch", "rotate_immediate", "shift_left", "shift_right", "shift_rotate",
    "signed", "simd_index_register", "simd_lane_index", "simd_list_member",
    "simd_scalar", "simd_vector", "simd_width_b8", "simd_width_d64",
    "simd_width_h16", "simd_width_q128", "simd_width_s32", "sp_allowed",
    "system_encoding", "unsigned", "writeback_post_index", "writeback_pre_index",
    "zr_allowed",
)
SEMANTIC_FLAG_BITS = {name: index for index, name in enumerate(SEMANTIC_FLAG_NAMES)}

# Operand kind/flag hints are useful for presentation and diagnostics, but
# they are not an encoder/decoder contract until a data-flow implementation
# exists.  Carry that fact in the C projection so consumers cannot mistake
# the hints for authoritative semantics.
CLASSIFICATION_STATUS_VALUES = {"presentation-only": 0}

# These four half-precision reduction rows use the XML's shared decode.  The
# ``V`` destination-width token is intentionally field-free: its value is
# supplied by the H variant and the Q arrangement table, while ``d`` and
# ``Vn`` still bind directly to Rd/Rn.  Keep this small proof table in the
# compiler rather than treating those rows as unresolved gaps.
SHARED_DECODE_PROOFS: dict[str, dict[str, Any]] = {
    "arm-a64@2026-06:FMAXNMV_asimdall_only_H": {
        "shared_decode_id": "FMAXNMV_asimdall_only_SD",
        "llvm_mc_word": "0x0e30c820",
    },
    "arm-a64@2026-06:FMAXV_asimdall_only_H": {
        "shared_decode_id": "FMAXV_asimdall_only_SD",
        "llvm_mc_word": "0x0e30f820",
    },
    "arm-a64@2026-06:FMINNMV_asimdall_only_H": {
        "shared_decode_id": "FMINNMV_asimdall_only_SD",
        "llvm_mc_word": "0x0eb0c820",
    },
    "arm-a64@2026-06:FMINV_asimdall_only_H": {
        "shared_decode_id": "FMINV_asimdall_only_SD",
        "llvm_mc_word": "0x0eb0f820",
    },
}

ATOMIC_SYSTEM_BARRIER_IDS = {
    "arm-a64@2026-06:CLREX_BN_barriers",
    "arm-a64@2026-06:DMB_BO_barriers",
    "arm-a64@2026-06:DSB_BO_barriers",
    "arm-a64@2026-06:ISB_BI_barriers",
}


def norm(value: str | None) -> str:
    return re.sub(r"\s+", " ", value or "").strip()


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            h.update(block)
    return h.hexdigest()


def source_tree_sha256(source: Path) -> str:
    """Hash the same top-level regular-file scope as the canonical importer."""
    digest = hashlib.sha256()
    files = sorted((path for path in source.iterdir() if path.is_file() and not path.is_symlink()),
                   key=lambda path: path.name.encode("utf-8"))
    for path in files:
        name = path.name.encode("utf-8")
        payload = path.read_bytes()
        digest.update(name)
        digest.update(b"\0")
        digest.update(struct.pack("<Q", len(payload)))
        digest.update(payload)
    return digest.hexdigest()


def text(element: ET.Element | None) -> str:
    return norm("".join(element.itertext())) if element is not None else ""


def base_link(link: str) -> str:
    return re.sub(r"__\d+$", "", link or "")


def mnemonic(assembly: str) -> str:
    token = (assembly or "").strip().split(None, 1)[0] if assembly else ""
    return re.sub(r"\{.*$", "", token).rstrip(".")


TOKEN_RE = re.compile(r"(?<![A-Za-z0-9_])(?:[A-Za-z_][A-Za-z0-9_]*)(?:\[[0-9:]+\])?")
CONCAT_RE = re.compile(r"^\(\s*([^()]*)\s*\)$")
SLICE_RE = re.compile(r"^([A-Za-z_][A-Za-z0-9_]*)(?:\[([0-9]+)(?::([0-9]+))?\])?$")
RANGE_RE = re.compile(r"(?:range|in the range)\s+([^.;]+)", re.I)

ARRANGEMENT_BASES = {"T_option", "Ta_option", "Ta_option_new", "Tb_option", "Tb_option_new", "Tb_option__Q1bit", "Ts_option", "V_option", "Va_option", "Vb_option", "s_2_option"}
CONDITION_BASES = {"cond_option"}
SHIFT_BASES = {"shift", "shift_option", "amount", "amount_option", "immh_shift", "immh_shift__new", "UIntimmhimmb64", "r_128UIntimmhimmb"}
EXTEND_BASES = {"extend_option"}
INDEX_BASES = {"index", "index_option", "imm5_index", "imm420"}
LABEL_BASES = {"imm14_offset", "imm19_offset", "imm26_offset", "immhiimmlo_offset"}
FP_BASES = {"fbits", "a_b_c_d_e_f_g_h", "immh_shift", "immh_shift__new"}
SYSTEM_BASES = {"MRS_values", "CRm_SY", "CRm_op2", "CRm_option", "Cm", "Cn", "at_op_option", "dc_op_option", "ic_op_option", "msr_imm", "op0_option", "op1", "op2", "pstatefield_option", "tlbi_op_option", "option"}
PREFETCH_BASES = {"Rt_prfop", "Rt_imm5"}
MEMORY_OFFSET_BASES = {"pimm", "simm", "S_imm9", "imm_option"}
HINT_MNEMONICS = {"AUTIA1716", "AUTIASP", "AUTIAZ", "AUTIB1716", "AUTIBSP", "AUTIBZ", "CSDB", "ESB", "NOP", "PACIA1716", "PACIASP", "PACIAZ", "PACIB1716", "PACIBSP", "PACIBZ", "SEVL", "SEV", "TSB", "WFE", "WFI", "YIELD", "XPACLRI"}
BARRIER_MNEMONICS = {"CLREX", "DMB", "DSB", "ISB", "PSSBB", "SB", "SSBB"}
SYSTEM_MNEMONICS = {"AT", "DC", "IC", "MRS", "MSR", "SYS", "SYSL", "TLBI"}


def parse_table(definition: ET.Element) -> dict[str, Any] | None:
    table = definition.find("table")
    tgroup = table.find("tgroup") if table is not None else None
    if table is None or tgroup is None:
        return None

    def cells(row: ET.Element) -> list[str]:
        return [norm("".join(cell.itertext())) for cell in row.findall("entry")]

    def cell_classes(row: ET.Element) -> list[str | None]:
        return [norm(cell.get("class")) or None for cell in row.findall("entry")]

    head = tgroup.find("thead")
    body = tgroup.find("tbody")
    header_row = head.find("row") if head is not None else None
    body_rows = body.findall("row") if body is not None else []
    return {
        "class": table.get("class"),
        "cols": tgroup.get("cols"),
        "header": cells(header_row) if header_row is not None else [],
        "header_classes": cell_classes(header_row) if header_row is not None else [],
        "rows": [cells(row) for row in body_rows],
        "row_classes": [cell_classes(row) for row in body_rows],
    }


def compact_table(table: dict[str, Any] | None) -> dict[str, Any] | None:
    """Select the key columns and the one semantic result column.

    Arm's tables put prose columns (``Description`` and ``Architectural
    Feature``) after the result.  The result is normally an angle-bracketed
    semantic name; the widening/narrowing tables use a literal header such as
    ``2`` and carry ``[present]``/``[absent]`` values.  Do not guess by taking
    the first two cells: doing that silently discards the target arrangement.
    """
    if not table:
        return None
    header = [norm(str(value)) for value in table.get("header", [])]
    header_classes = table.get("header_classes", [])
    row_classes = table.get("row_classes", [])
    if not header:
        raise RuntimeError("value table has no header")
    result_indices = [index for index, value in enumerate(header) if re.search(r"<[^>]+>", value)]
    if not result_indices:
        # The only pinned tables without an angle-bracket result name use a
        # literal result column (currently the fixed ``2`` selector).
        result_indices = [len(header) - 1]
    if len(result_indices) != 1:
        raise RuntimeError(f"value table must expose exactly one result column: {header!r}")
    result_index = result_indices[0]
    key_indices = list(range(result_index))
    if not key_indices:
        raise RuntimeError(f"value table has no explicit key columns: {header!r}")
    rows = []
    for row_index, row in enumerate(table.get("rows", [])):
        if len(row) <= result_index:
            raise RuntimeError(f"value table row is missing its result column: {header!r}: {row!r}")
        if any(not norm(str(row[index])) for index in key_indices):
            raise RuntimeError(f"value table row is missing an explicit key: {header!r}: {row!r}")
        classes = row_classes[row_index] if row_index < len(row_classes) else []
        rows.append({"key": [norm(str(row[index])) for index in key_indices],
                     "key_classes": [classes[index] if index < len(classes) else None for index in key_indices],
                     "result": norm(str(row[result_index])),
                     "result_class": classes[result_index] if result_index < len(classes) else None})
    return {
        "key_headers": [header[index] for index in key_indices],
        "key_header_classes": [header_classes[index] if index < len(header_classes) else None for index in key_indices],
        "result_header": header[result_index],
        "result_class": header_classes[result_index] if result_index < len(header_classes) else None,
        "key_arity": len(key_indices),
        "result_arity": 1,
        "rows": rows,
    }


CONSTRAINT_TOKEN_RE = re.compile(r"(?:[A-Za-z_][A-Za-z0-9_]*|0[bB][01xX]+|0[xX][0-9A-Fa-f]+|[01xX]+|==|!=|<=|>=|&&|\|\||[(){}\[\],:+*/<>!&|=-])")


def constraint_program(expression: str | None) -> list[str]:
    """Emit a compact token stream instead of reproducing XML expressions."""
    return CONSTRAINT_TOKEN_RE.findall(expression or "")


def parse_formula_program(value: str) -> list[dict[str, Any]] | None:
    """Parse the small arithmetic vocabulary used by Arm value tables.

    This deliberately returns an AST-like VM program rather than copying the
    XML formula.  Unsupported formulas remain an explicit generation error so
    they cannot be mistaken for executable semantics.
    """
    value = norm(value)
    # UInt(<concat>) with an optional constant adjustment.
    match = re.fullmatch(r"UInt\(([^()]*)\)\s*([+-])?\s*(\d+)?", value, re.I)
    if not match:
        match = re.fullmatch(r"(\d+)\s*-\s*UInt\(([^()]*)\)", value, re.I)
        if match:
            constant = int(match.group(1))
            body = match.group(2)
            adjustment = {"op": "sub_from_const", "value": constant}
        else:
            return None
    else:
        body = match.group(1)
        operator = match.group(2)
        amount = match.group(3)
        adjustment = None
        if operator and amount:
            adjustment = {"op": "add_const", "value": int(amount) * (1 if operator == "+" else -1)}
    parts = []
    for part in [norm(x.strip(" '\"")) for x in re.split(r"\s*::\s*|\s*:(?!\d+\])\s*", body) if norm(x.strip(" '\""))]:
        slice_match = re.fullmatch(r"([A-Za-z_][A-Za-z0-9_]*)(?:\[([0-9]+)(?::([0-9]+))?\])?", part)
        if not slice_match:
            if re.fullmatch(r"\d+", part):
                parts.append({"op": "literal", "value": int(part)})
                continue
            return None
        name, high, low = slice_match.groups()
        item: dict[str, Any] = {"op": "field", "name": name}
        if high is not None:
            item.update({"high": int(high), "low": int(low if low is not None else high)})
        parts.append(item)
    if not parts:
        return None
    program: list[dict[str, Any]] = [{"op": "uint_concat", "parts": parts}]
    if adjustment is not None:
        program.append(adjustment)
    return program


# These are the bounded numeric relations that are present in the pinned
# Apple-M1 corpus.  Keep the source patterns deliberately narrow: ordinary
# prose mentioning a shift/range is not a relation, while each selected
# relation must either become a normalized VM program or an explicit gap.
NUMERIC_UINT_RE = re.compile(
    r"\b(?:\d+\s*-\s*)?(?:UInt|SInt)\s*\([^()]{1,120}\)(?:\s*[+-]\s*\d+)?",
    re.I,
)
NUMERIC_BRANCH_TIMES4_RE = re.compile(
    r'encoded\s+as\s+"(imm(?:9|14|19|26))"\s+times\s+4',
    re.I,
)
NUMERIC_FCVT_SCALE_RE = re.compile(r'encoded\s+as\s+64\s+minus\s+"scale"', re.I)
NUMERIC_REGISTER_MOD_RE = re.compile(
    r'encoded\s+as\s+"(Rt|Rn)"\s+plus\s+([123])\s+modulo\s+32',
    re.I,
)


def encodedin_program(encodedin: str | None, canonical_fields: set[str]) -> list[dict[str, Any]] | None:
    """Normalize an ``encodedin`` field reference to a small VM program."""
    value = norm(encodedin).strip()
    if value.startswith("(") and value.endswith(")"):
        value = norm(value[1:-1])
    if not value:
        return None
    parts = [norm(part.strip(" '\"")) for part in re.split(r"\s*::\s*|\s*:(?!\d+\])\s*", value) if norm(part.strip(" '\""))]
    if not parts:
        return None
    parsed: list[dict[str, Any]] = []
    for part in parts:
        match = re.fullmatch(r"([A-Za-z_][A-Za-z0-9_]*)(?:\[([0-9]+)(?::([0-9]+))?\])?", part)
        if not match:
            return None
        name, high, low = match.groups()
        # Keep the source binding honest.  The XML symbol can use a display
        # name that is not an encoded field; in that case the relation is a
        # residual rather than an invented field binding.
        if name not in canonical_fields:
            return None
        item: dict[str, Any] = {"op": "field", "name": name}
        if high is not None:
            item.update({"high": int(high), "low": int(low if low is not None else high)})
        parsed.append(item)
    return parsed if len(parsed) == 1 else [{"op": "uint_concat", "parts": parsed}]


def numeric_relation_descriptor(intro: str, encodedin: str | None, canonical_fields: set[str]) -> tuple[dict[str, Any], list[dict[str, Any]]] | None:
    """Return ``(normalized relation key, VM program)`` for selected XML prose."""
    intro = intro or ""
    base = encodedin_program(encodedin, canonical_fields)
    if base is None:
        # UInt formulas carry their own encoded field expression; the parser
        # below validates the quoted body against the row's fields separately
        # through the normalized formula shape.
        base = []
    uint_match = NUMERIC_UINT_RE.search(intro)
    if uint_match:
        formula = uint_match.group(0)
        program = parse_formula_program(formula)
        if program is None:
            return {"kind": "integer_decode", "value": "unparsed"}, []
        def fields_are_bound(value: Any) -> bool:
            if isinstance(value, dict):
                if value.get("op") == "field" and value.get("name") not in canonical_fields:
                    return False
                return all(fields_are_bound(item) for item in value.values())
            if isinstance(value, list):
                return all(fields_are_bound(item) for item in value)
            return True
        if not fields_are_bound(program):
            return {"kind": "integer_decode", "value": "unparsed"}, []
        adjustment: dict[str, Any] = {"kind": "integer_decode", "value": 0}
        if re.search(r"\)\s*-\s*\d+\s*$", formula):
            adjustment["kind"] = "sub_const"
            adjustment["value"] = int(re.search(r"-\s*(\d+)\s*$", formula).group(1))
        elif re.match(r"^\d+\s*-", formula):
            adjustment["kind"] = "sub_from_const"
            adjustment["value"] = int(re.match(r"^(\d+)", formula).group(1))
        return adjustment, program
    branch_match = NUMERIC_BRANCH_TIMES4_RE.search(intro)
    if branch_match:
        if not base:
            return {"kind": "scale_mul", "value": 4}, []
        field_name = branch_match.group(1).lower()
        field_width = {"imm9": 9, "imm14": 14, "imm19": 19, "imm26": 26}[field_name]
        return {"kind": "scale_mul", "value": 4}, base + [{"op": "sign_extend", "bits": field_width}, {"op": "scale_mul", "value": 4}]
    if NUMERIC_FCVT_SCALE_RE.search(intro):
        if not base:
            return {"kind": "sub_from_const", "value": 64}, []
        return {"kind": "sub_from_const", "value": 64}, base + [{"op": "sub_from_const", "value": 64}]
    register_match = NUMERIC_REGISTER_MOD_RE.search(intro)
    if register_match:
        field_name, delta_text = register_match.groups()
        # The relation is a register-number projection, not a bitwise
        # arithmetic transform.  Keep it as a dedicated VM operation.
        if norm(encodedin) != field_name or field_name not in canonical_fields:
            return {"kind": "register_add_mod", "value": int(delta_text), "modulus": 32}, []
        return (
            {"kind": "register_add_mod", "value": int(delta_text), "modulus": 32},
            [{"op": "register_add_mod", "field": field_name, "delta": int(delta_text), "modulus": 32}],
        )
    return None


def program_text(program: list[dict[str, Any]] | None) -> str:
    return json.dumps(program or [], sort_keys=True, separators=(",", ":"))


def normalize_table_value(value: str, *, result: bool = False, field_width: int | None = None,
                          source_class: str | None = None) -> dict[str, Any]:
    """Normalize a compact table atom to a typed semantic value.

    Bit patterns remain strings so leading zeroes and ``x`` don't disappear;
    decimal literals become integers; architectural names become enums; and
    the small set of XML expressions (``UInt(...)`` and arithmetic shifts)
    remains an explicitly typed expression rather than raw prose.  The Arm
    XML entry class is authoritative for ambiguous numeric cells: a
    ``bitfield`` cell is parsed as binary, while cells with no class use the
    bounded field-width fallback below.
    """
    value = norm(value)
    source_class = norm(source_class).lower() or None
    if not value or len(value) > 96:
        raise RuntimeError(f"empty/overlong value-table atom: {value!r}")
    if value.startswith("[") and value.endswith("]"):
        marker = norm(value[1:-1]).lower()
        if marker in {"absent", "present", "reserved"}:
            return {"type": "enum", "value": marker.upper() if marker == "reserved" else marker}
        raise RuntimeError(f"unsupported bracketed value-table atom: {value!r}")
    integer = re.fullmatch(r"#?[-+]?\d+", value)
    if integer:
        unsigned_digits = re.fullmatch(r"[01]+", value)
        if source_class == "bitfield" and unsigned_digits:
            if len(value) == 1 and value in "01" and (field_width is None or field_width == 1):
                return {"type": "integer", "value": int(value)}
            return {"type": "bits", "value": value.lower()}
        # Missing entry classes are rare in the pinned tables.  Use the
        # encoded width only as a fallback for those cells, preserving genuine
        # decimal values such as 10/11 when they fit a wider field and were
        # not marked as bitfield in the source.
        if source_class is None and field_width is not None and field_width > 1 and unsigned_digits:
            decimal_value = int(value, 10)
            if len(value) == field_width or decimal_value >= (1 << field_width):
                return {"type": "bits", "value": value.lower()}
        return {"type": "integer", "value": int(value.lstrip("#"), 10)}
    if re.fullmatch(r"[01xX]+", value):
        # A one-bit 0/1 cell is a scalar integer; multi-bit cells preserve
        # their width and wildcard positions as a bit-pattern.
        if len(value) == 1 and value in "01" and (source_class != "bitfield" or field_width in (None, 1)):
            return {"type": "integer", "value": int(value)}
        return {"type": "bits", "value": value.lower()}
    if re.fullmatch(r"0[xX][0-9a-fA-F]+", value):
        return {"type": "integer", "value": int(value, 16)}
    # Shift/extend spellings are architectural enums, even when they contain
    # a display ``#0`` suffix.
    if re.fullmatch(r"(?:LSL|LSR|ASR|ROR)(?:\s+#[-+]?\d+)?(?:\|(?:UXTW|UXTX))?", value):
        return {"type": "enum", "value": value}
    expression = bool(re.search(r"\b(?:UInt|SInt)\b|::|[+\-*/]", value))
    if expression:
        # Reject sentence-like cells while allowing architectural arithmetic.
        if re.search(r"\b(?:for|more|information|specifies|required|encoded|this|the)\b", value, re.I):
            raise RuntimeError(f"narrative value-table atom: {value!r}")
        program = parse_formula_program(value)
        if program is None:
            raise RuntimeError(f"unsupported semantic value-table formula: {value!r}")
        return {"type": "program", "program": program}
    if re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9_|]*", value):
        return {"type": "enum", "value": value}
    if result:
        raise RuntimeError(f"unrecognized semantic result atom: {value!r}")
    # Key cells are still bounded semantic tokens (for example ``immh``
    # wildcards); retaining them as enum values is safer than dropping them.
    return {"type": "enum", "value": value}


def table_field_width(header: str, field_widths: dict[str, int]) -> int | None:
    """Resolve a valuetable key header to its encoded bit width.

    Most headers are direct field names, while a few Arm tables name a slice
    (for example ``cmode[2:1]``).  Alias rows can also carry an incomplete
    field map; callers may supplement this result with the fixed-width format
    inferred from the table's own bitfield cells.
    """
    if header in field_widths:
        return field_widths[header]
    match = re.fullmatch(r"([A-Za-z_][A-Za-z0-9_]*)(?:\[([0-9]+)(?::([0-9]+))?\])?", header)
    if not match or match.group(1) not in field_widths:
        return None
    first = match.group(2)
    if first is None:
        return field_widths[match.group(1)]
    second = match.group(3)
    return abs(int(first) - int(second if second is not None else first)) + 1


def normalized_value_table(table: dict[str, Any] | None, field_widths: dict[str, int] | None = None,
                           class_census: collections.Counter | None = None) -> dict[str, Any]:
    compact = compact_table(table)
    if compact is None:
        raise RuntimeError("value-table transform has no source table")
    entries = []
    keys_seen: set[str] = set()
    results_seen: set[str] = set()
    field_widths = field_widths or {}
    if class_census is None:
        class_census = collections.Counter()
    key_widths = [table_field_width(header, field_widths) for header in compact["key_headers"]]
    fallback_widths: list[int | None] = [None] * len(key_widths)
    for index in range(len(key_widths)):
        if key_widths[index] is not None:
            continue
        missing_values = [row["key"][index] for row in compact["rows"]
                          if index >= len(row.get("key_classes", [])) or row["key_classes"][index] is None]
        bit_lengths = [len(value) for value in missing_values if re.fullmatch(r"[01xX]+", value)]
        if bit_lengths and len(bit_lengths) == len(missing_values) and len(set(bit_lengths)) == 1 and bit_lengths[0] > 1:
            fallback_widths[index] = bit_lengths[0]
            class_census["inferred_width_columns"] += 1
    for row in compact["rows"]:
        key = []
        key_classes = row.get("key_classes", [])
        for index, (value, width) in enumerate(zip(row["key"], key_widths)):
            source_class = key_classes[index] if index < len(key_classes) else None
            class_census["key_cells"] += 1
            if source_class == "bitfield":
                class_census["key_bitfield_cells"] += 1
            elif source_class:
                class_census["key_non_bitfield_cells"] += 1
            else:
                class_census["key_missing_class_cells"] += 1
            if value in {"10", "11"}:
                class_census["numeric_10_11_cells"] += 1
                class_census["bitfield_numeric_10_11_cells" if source_class == "bitfield" else "non_bitfield_numeric_10_11_cells"] += 1
            effective_width = width if width is not None else fallback_widths[index]
            if source_class is None and width is None and effective_width is not None:
                class_census["key_inferred_width_cells"] += 1
            if source_class is None and effective_width is not None:
                class_census["key_fallback_cells"] += 1
            key.append(normalize_table_value(value, field_width=effective_width, source_class=source_class))
        result = [normalize_table_value(row["result"], result=True)]
        key_signature = json.dumps(key, sort_keys=True, separators=(",", ":"))
        if key_signature in keys_seen:
            raise RuntimeError(f"duplicate value-table key: {key!r}")
        keys_seen.add(key_signature)
        result_signature = json.dumps(result, sort_keys=True, separators=(",", ":"))
        results_seen.add(result_signature)
        entries.append({"key": key, "result": result})
    if not entries:
        raise RuntimeError("value-table transform has no rows")
    invertible = len(results_seen) == len(entries)
    return {
        "key_headers": compact["key_headers"],
        "result_header": compact["result_header"],
        "key_arity": compact["key_arity"],
        "result_arity": 1,
        "entries": entries,
        "unique_keys": True,
        "unique_results": invertible,
        "invertibility_reason": "one_to_one" if invertible else "result_collisions",
    }


def explanation_records(root: ET.Element, filename: str) -> dict[str, list[dict[str, Any]]]:
    result: dict[str, list[dict[str, Any]]] = collections.defaultdict(list)
    explanations = root.find("explanations")
    if explanations is None:
        return result
    for explanation in explanations.findall("explanation"):
        enclist = [x.strip() for x in explanation.get("enclist", "").split(",") if x.strip()]
        symbol = explanation.find("symbol")
        if symbol is None or not symbol.get("link"):
            continue
        children = [child for child in explanation.iter() if child.tag in ("account", "definition")]
        for child in children:
            record = {
                "xml_file": filename,
                "enclist": enclist,
                "symbol": text(symbol),
                "link": symbol.get("link"),
                "kind": child.tag,
                "encodedin": child.get("encodedin"),
                "intro": text(child.find("intro")),
                "table": parse_table(child) if child.tag == "definition" else None,
            }
            for encoding_name in enclist:
                result[encoding_name].append(record)
    return result


def source_index(source: Path) -> tuple[dict[tuple[str, str], ET.Element], dict[tuple[str, str], list[dict[str, Any]]]]:
    encodings: dict[tuple[str, str], ET.Element] = {}
    explanations: dict[tuple[str, str], list[dict[str, Any]]] = collections.defaultdict(list)
    for path in sorted(source.glob("*.xml")):
        root = ET.parse(path).getroot()
        filename = path.name
        for encoding in root.iter("encoding"):
            name = encoding.get("name")
            if name:
                encodings[(filename, name)] = encoding
        for name, records in explanation_records(root, filename).items():
            explanations[(filename, name)].extend(records)
    return encodings, explanations


def numeric_relation_source_keys(rows: list[dict[str, Any]], explanation_index: dict[tuple[str, str], list[dict[str, Any]]]) -> list[dict[str, Any]]:
    """Derive the selected numeric-relation corpus directly from source XML."""
    result: list[dict[str, Any]] = []
    for row in rows:
        if row.get("kind") != "canonical":
            continue
        key = (row["iform_file"], row["encoding_name"])
        fields = {field["name"] for field in row.get("fields", [])}
        for explanation in explanation_index.get(key, []):
            relation = numeric_relation_descriptor(explanation.get("intro", ""), explanation.get("encodedin"), fields)
            if relation is None:
                continue
            descriptor, _ = relation
            result.append({
                "row_id": row["id"],
                "operand_link": explanation.get("link") or "",
                **descriptor,
            })
    result.sort(key=lambda value: json.dumps(value, sort_keys=True, separators=(",", ":")))
    return result


def anchor_info(encoding: ET.Element) -> list[dict[str, Any]]:
    template = encoding.find("asmtemplate")
    result = []
    if template is None:
        return result
    for position, anchor in enumerate(template.iter("a")):
        result.append({
            "position": position,
            "link": anchor.get("link"),
            "symbol": text(anchor),
            "hover": anchor.get("hover"),
        })
    return result


def register_widths(link: str, symbol: str, intro: str) -> list[str]:
    value = f"{link} {symbol} {intro}"
    result = []
    if re.search(r"\b32[- ]bit\b", value, re.I) or re.match(r"^W", link):
        result.append("w32")
    if re.search(r"\b64[- ]bit\b", value, re.I) or re.match(r"^X", link):
        result.append("x64")
    if "width specifier" in value.lower() and link.startswith("R"):
        result.append("dynamic")
    return sorted(set(result))


def simd_widths(link: str, intro: str) -> list[str]:
    value = f"{link} {intro}"
    result = []
    for bits, name in (("8", "b8"), ("16", "h16"), ("32", "s32"), ("64", "d64"), ("128", "q128")):
        if re.search(rf"\b{bits}[- ]bit\b", value, re.I):
            result.append(name)
    for prefix, name in (("H", "h16"), ("S", "s32"), ("D", "d64"), ("Q", "q128"), ("B", "b8")):
        if link.startswith(prefix) and link != "Bt":
            result.append(name)
    return sorted(set(result))


def classify(link: str, symbol: str, assembly: str, explanations: list[dict[str, Any]]) -> tuple[list[str], list[str]]:
    base = base_link(link)
    lower = " ".join([link, symbol] + [x.get("intro", "") for x in explanations]).lower()
    kinds: set[str] = set()
    flags: set[str] = set()
    mnem = mnemonic(assembly)
    if base in SYSTEM_BASES or any(x in lower for x in ("system register", "pstate field", "system instruction")):
        kinds.add("system_operation"); flags.add("system_encoding")
    if base == "MRS_values" or "system register name" in lower:
        kinds.add("system_register")
    if base in {"CRm_SY", "CRm_option", "option"} and mnem in BARRIER_MNEMONICS:
        kinds.add("barrier_option"); flags.add("barrier")
    if base in PREFETCH_BASES:
        kinds.add("prefetch_operation"); flags.add("prefetch")
    if base in ARRANGEMENT_BASES:
        kinds.add("simd_arrangement"); flags.add("arrangement_selector")
        if base in {"V_option", "Va_option", "Vb_option"} or "width specifier" in lower:
            kinds.add("simd_width_selector")
    if base in INDEX_BASES or symbol.lower() in {"<index>", "<index1>", "<index2>"}:
        kinds.add("simd_lane"); flags.add("simd_lane_index")
    gpr = bool(re.match(r"^(?:W|X)(?:[admnstr]|d|n|m|s|t)", link) or link in {"Rm_option", "Rn_option", "RmRn_option", "Rt_option", "Xn"} or "general-purpose" in lower or "stack pointer" in lower or "zr" in lower)
    if gpr:
        if "width specifier" in lower and link.startswith("R"):
            kinds.add("gpr_width_selector")
        else:
            kinds.add("gpr_register")
            flags.update(f"gpr_width_{x}" for x in register_widths(link, symbol, " ".join(x.get("intro", "") for x in explanations)))
            evidence = f"{link} {symbol} {lower}"
            if "stack pointer" in evidence or "|sp" in evidence or "sp>" in evidence: flags.add("sp_allowed")
            if "zr" in evidence or "orwzr" in link.lower() or "orxzr" in link.lower(): flags.add("zr_allowed")
            if "defaults to x30" in evidence: flags.add("optional_defaults_x30")
    # Link identifiers are case-sensitive architectural tokens.  In
    # particular, lowercase ``simm`` is a scalar immediate, not an ``S``
    # SIMD register; never classify it by a case-insensitive prefix.
    simd = "simd&fp" in lower or "simd/fp" in lower or "simd" in lower or bool(re.match(r"^(?:[BHSDQ]|V)", link)) or link in {"d", "n__2", "n__3", "m__2", "V_hv", "M_Rm"}
    if simd:
        kinds.add("simd_register")
        if "scalar" in lower or re.match(r"^[HSD]", link): flags.add("simd_scalar")
        if "vector" in lower or link.startswith("V") or "<V" in symbol: flags.add("simd_vector")
        if "table register" in lower or re.search(r"(?:VnPlus|Vt[234]|[DHSQ]t[12])", link): kinds.add("simd_list"); flags.add("simd_list_member")
        if "index register" in lower: flags.add("simd_index_register")
        if base in INDEX_BASES or "element index" in lower or "index register" in lower: kinds.add("simd_lane"); flags.add("simd_lane_index")
        if base in ARRANGEMENT_BASES or "arrangement specifier" in lower or "element size specifier" in lower: kinds.add("simd_arrangement"); flags.add("arrangement_selector")
        flags.update(f"simd_width_{x}" for x in simd_widths(link, " ".join(x.get("intro", "") for x in explanations)))
    if base in CONDITION_BASES or "condition code" in lower or symbol.lower() in {"<cond>", "<invcond>"}:
        kinds.add("condition"); flags.add("condition_field")
        if "invcond" in symbol.lower() or "inverse" in lower: flags.add("condition_inverted")
    if base == "nzcv" or "nzcv condition flags" in lower: kinds.add("nzcv_flags"); flags.add("nzcv_4bit")
    if base in LABEL_BASES or "program label" in lower or "<label>" in symbol.lower():
        kinds.add("label_fixup"); flags.add("pc_relative")
        if "imm26" in base: flags.add("branch_rel26")
        elif "imm19" in base: flags.add("branch_rel19")
        elif "imm14" in base: flags.add("branch_rel14")
        elif "immhiimmlo" in base: flags.add("page_relative" if "page" in lower else "pc_relative_adr")
    if base in SHIFT_BASES or "shift amount" in lower or "shift to apply" in lower:
        kinds.add("shift")
        if "left" in lower: flags.add("shift_left")
        if "right" in lower: flags.add("shift_right")
        if "rotate" in lower: flags.add("shift_rotate")
    if base in EXTEND_BASES or "extend option" in lower: kinds.add("extend"); flags.add("extend_option")
    if "rotation" in lower or base.startswith("rotate_option"): kinds.add("rotate"); flags.add("rotate_immediate")
    immediate = base in {"imm", "imm__bitmask", "imm_0_63", "imm6", "imm14_offset", "imm19_offset", "imm26_offset", "imm420", "imm5_index", "immhiimmlo_offset", "immr", "imms", "lsb", "mask", "msr_imm", "nzcv", "op1", "op2", "pimm", "simm", "S_imm9", "width", "hw_imm16", "option", "Rt_imm5", "CRm_op2", "a_b_c_d_e_f_g_h"} or symbol.lower() in {"<imm>", "<simm>", "<pimm>", "<imm5>", "<imm6>", "<amount>", "<width>", "<lsb>", "<immr>", "<imms>", "<mask>", "<nzcv>"} or "immediate" in lower
    if immediate and not ("system" in lower and base not in {"CRm_op2"}):
        kinds.add("integer_immediate")
        if "unsigned" in lower: flags.add("unsigned")
        if "signed" in lower: flags.add("signed")
        if "optional" in lower: flags.add("optional")
        if "defaulting to 0" in lower or "defaulting to zero" in lower: flags.add("default_zero")
        if "bitmask" in lower or "bit mask" in lower: flags.add("bitmask_transform")
        if base in INDEX_BASES: flags.add("index_immediate")
        if base in MEMORY_OFFSET_BASES or "byte offset" in lower: kinds.add("memory_offset"); flags.add("memory_offset")
    if base in FP_BASES or "fixed-point" in lower or "<fbits>" in symbol.lower(): kinds.add("fp_immediate"); flags.add("fp_or_fixed_point")
    if base == "a_b_c_d_e_f_g_h" and mnem == "FMOV": kinds.add("fp_immediate"); flags.add("fp_imm8_encoding")
    if "[" in assembly and ("base register" in lower or "|sp>" in symbol.lower()):
        kinds.add("memory_base"); flags.add("memory_base")
        if "]!" in assembly or "], #" in assembly: flags.add("memory_writeback")
        if "post" in assembly.lower() or "]," in assembly: flags.add("writeback_post_index")
        if "]!" in assembly: flags.add("writeback_pre_index")
        if "register to be transferred" in lower or "register to be loaded" in lower: kinds.add("memory_data_register")
    if base == "s_2_option" or symbol.strip() == "2": kinds.add("fixed_constant"); flags.add("fixed_literal_2")
    if base == "V_hv": kinds.add("simd_prefix_selector")
    if not kinds: kinds.add("other")
    return sorted(kinds), sorted(flags)


def fields_from_expr(encodedin: str | None, canonical_fields: set[str]) -> tuple[list[str], dict[str, Any] | None]:
    value = norm(encodedin)
    if not value:
        return [], None
    match = CONCAT_RE.match(value)
    if match:
        parts = [norm(part.strip(" '")) for part in match.group(1).split("::") if norm(part.strip(" '"))]
        return [x for x in parts if x in canonical_fields], {"kind": "concat", "parts": parts, "invertible": True}
    match = SLICE_RE.match(value)
    if match:
        name, high, low = match.groups()
        if name in canonical_fields:
            transform = None if high is None else {"kind": "slice", "field": name, "high": int(high), "low": int(low if low is not None else high), "invertible": True}
            return [name], transform
    tokens = []
    for token in TOKEN_RE.findall(value):
        if token in canonical_fields and token not in tokens:
            tokens.append(token)
    return tokens, None


def transform_records(operand: dict[str, Any], explanations: list[dict[str, Any]], canonical_fields: set[str],
                      field_widths: dict[str, int] | None = None,
                      class_census: collections.Counter | None = None) -> list[dict[str, Any]]:
    result = []
    for explanation_index, explanation in enumerate(explanations):
        relation = numeric_relation_descriptor(explanation.get("intro", ""), explanation.get("encodedin"), canonical_fields)
        relation_key = None
        relation_program = None
        if relation is not None:
            relation_key, relation_program = relation
            relation_key = {
                "row_id": operand.get("form_id", ""),
                "operand_link": operand.get("link", ""),
                **relation_key,
            }
        fields, transform = fields_from_expr(explanation.get("encodedin"), canonical_fields)
        if transform is not None:
            transform.update({"source": explanation_index})
            result.append(transform)
        elif explanation.get("encodedin") and not fields:
            result.append({"kind": "overlay_required", "source": explanation_index, "reason": "unresolved_encodedin", "invertible": False})
        intro = explanation.get("intro", "")
        lower = intro.lower()
        if relation is not None:
            if relation_program:
                result.append({"kind": "integer_decode", "source": explanation_index, "program": relation_program, "relation_key": relation_key, "invertible": True})
            else:
                result.append({"kind": "overlay_required", "source": explanation_index, "reason": "unparsed_numeric_relation", "relation_key": relation_key, "invertible": False})
        if "/" in intro or "2^" in intro or "scale" in lower or "shift" in lower:
            factors = re.findall(r"(?:/[0-9]+|2\s*\^\s*-?[0-9]+|(?:left|right)?\s*shift(?:ed)?(?:\s+by)?\s*[0-9]+)", intro, re.I)
            # Selected numeric relations above already have a normalized VM
            # program.  Do not append a presentation-only text transform for
            # the same source explanation.
            if factors and relation is None:
                program = []
                for factor in factors:
                    factor = norm(factor)
                    divisor = re.fullmatch(r"/(\d+)", factor)
                    power = re.fullmatch(r"2\s*\^\s*-(\d+)", factor)
                    if divisor:
                        program.append({"op": "scale_div", "value": int(divisor.group(1))})
                    elif power:
                        program.append({"op": "scale_pow2", "value": int(power.group(1))})
                    else:
                        program.append({"op": "text_factor", "value": factor})
                result.append({"kind": "text_transform", "source": explanation_index, "program": program, "invertible": False})
        if explanation.get("table"):
            table = normalized_value_table(explanation["table"], field_widths, class_census)
            # Keep only typed key/result entries.  Narrative XML prose and
            # description columns are intentionally omitted from artifacts.
            result.append({"kind": "value_table", "source": explanation_index, **table, "invertible": table["unique_results"]})
    return result


def ranges_from_text(intros: Iterable[str]) -> list[str]:
    result = set()
    for intro in intros:
        for match in RANGE_RE.finditer(intro):
            value = norm(match.group(1))
            numeric = re.match(r"[-+]?\d+(?:\s*(?:to|\.\.|-)\s*[-+]?\d+)?", value)
            if numeric:
                result.add(numeric.group(0))
    return sorted(result)


def owner_sets(rows: list[dict[str, Any]]) -> tuple[dict[str, str], dict[str, Any]]:
    gpr_header = Path(__file__).resolve().parents[6] / "src/buster/lib/compiler/assembly/generated/arm-a64-m1-gpr.generated.h"
    scalar_header = Path(__file__).resolve().parents[6] / "src/buster/lib/compiler/assembly/generated/arm-a64-m1-scalar-integer.generated.h"
    gpr_ids = set(re.findall(r'arm_row_id = "([^"]+)"', gpr_header.read_text())) if gpr_header.exists() else set()
    scalar_ids = set(re.findall(r'arm_row_id = "([^"]+)"', scalar_header.read_text())) if scalar_header.exists() else set()
    owners: dict[str, str] = {}
    for row in rows:
        rid = row["id"]
        fixed = row["fixed_mask"] == "0xffffffff" and row["field_mask"] == "0x00000000"
        if fixed: owner = "fixed32"
        elif rid in gpr_ids: owner = "direct_gpr"
        elif rid in scalar_ids: owner = "scalar_integer"
        elif row["system"]: owner = "system"
        else:
            assembly = row.get("assembly", "")
            memory = "[" in assembly and (row["instr_class"] in ("general", "fpsimd") or (row["instr_class"] == "advsimd" and re.match(r"^(?:LD|ST)[1-4]", assembly)))
            owner = "memory" if memory else ("general_nonmemory" if row["instr_class"] == "general" else "complex_simd_fp")
        owners[rid] = owner
    # The Arm XML owner census has a deliberate direct/complex split for
    # non-memory SIMD&FP rows.  Choose direct rows by evidence complexity, then
    # fill the exact bounded count deterministically (no family-specific API).
    candidates = [r for r in rows if owners[r["id"]] == "complex_simd_fp"]
    inv = {r["id"]: r for r in rows}
    def score(row: dict[str, Any]) -> tuple[int, str]:
        assembly = row.get("assembly", "")
        # register-only arrangements are the mechanically direct slice; FP,
        # lane, immediate, and split-field forms remain explicit complex rows.
        points = 0
        points += 8 if "#" not in assembly and "[" not in assembly else 0
        points += 3 if row["instr_class"] == "advsimd" else 0
        points += 1 if len(row.get("fields", [])) <= 4 else 0
        points -= 3 if any(token in assembly for token in ("<index>", "<shift>", "<rotate>", "{2}")) else 0
        return points, row["id"]
    candidates.sort(key=lambda row: (-score(row)[0], score(row)[1]))
    for row in candidates[:390]:
        owners[row["id"]] = "direct_simd"
    expected = {"fixed32": 32, "direct_gpr": 80, "scalar_integer": 72, "direct_simd": 390, "system": 18, "memory": 559, "general_nonmemory": 24, "complex_simd_fp": 348}
    counts = collections.Counter(owners.values())
    if dict(counts) != expected:
        raise RuntimeError(f"owner census mismatch: {dict(counts)} != {expected}")
    return owners, {"expected": expected, "actual": dict(sorted(counts.items()))}


def owner_mode(row: dict[str, Any], owner: str) -> str | None:
    assembly = row.get("assembly", "")
    if owner != "memory":
        return None
    if row["instr_class"] == "general":
        if re.search(r"\]!\s*$", assembly): return "general/pre"
        if re.search(r"\],\s*#", assembly): return "general/post"
        if re.search(r"\[[^]]*#", assembly): return "general/imm"
        if re.search(r"\[[^]]*<Xm", assembly): return "general/reg"
        return "general/base"
    if row["instr_class"] == "fpsimd":
        if re.search(r"\]!\s*$", assembly): return "FP/pre"
        if re.search(r"\],\s*", assembly): return "FP/post"
        if re.search(r"\[[^]]*#", assembly): return "FP/imm"
        return "FP/reg"
    if re.match(r"^(?:LD|ST)[1-4]", assembly):
        # The indexed-list syntax contains a `], [` before the base register;
        # inspect the base-register bracket itself so that delimiter is not
        # mistaken for a writeback mode.
        return "AdvSIMD-list/post" if re.search(r"\[<Xn\|SP>\]\s*,", assembly) else "AdvSIMD-list/base"
    return None


def cross_lens_sets(rows: list[dict[str, Any]], owners: dict[str, str]) -> tuple[dict[str, set[str]], dict[str, Any]]:
    """Build the deterministic overlapping atomic/control-flow lenses.

    The census is over canonical rows.  Aliases retain an empty lens list;
    their canonical target remains available through ``alias.target_id``.
    """
    canonical = [row for row in rows if row["kind"] == "canonical"]
    # All A64 acquire/release, exclusive, swap, CAS, and LSE memory forms are
    # identified by their architectural mnemonic families.  STLXP is the one
    # pair that does not share a prefix with the other ST exclusive forms.
    atomic_prefixes = (
        "CAS", "LDADD", "LDCLR", "LDEOR", "LDSET", "LDSMAX", "LDSMIN",
        "LDUMAX", "LDUMIN", "SWP", "LDAX", "LDXR", "LDXP", "STLXR",
        "STLXP", "STXR", "STXP", "LDAR", "LDAP", "LDLAR", "STLR",
        "STLLR", "STLUR",
    )
    atomic_memory = {
        row["id"] for row in canonical
        if owners.get(row["id"]) == "memory" and any(mnemonic(row.get("assembly", "")).startswith(prefix) for prefix in atomic_prefixes)
    }
    if len(atomic_memory) != 225:
        raise RuntimeError(f"atomic memory census mismatch: {len(atomic_memory)} != 225")
    atomic_system = {row["id"] for row in canonical if row["id"] in ATOMIC_SYSTEM_BARRIER_IDS}
    if atomic_system != ATOMIC_SYSTEM_BARRIER_IDS:
        raise RuntimeError("atomic system barrier census mismatch")

    control_general = {row["id"] for row in canonical if owners.get(row["id"]) == "general_nonmemory"}
    if len(control_general) != 24:
        raise RuntimeError(f"control general census mismatch: {len(control_general)} != 24")
    # Conditional floating-point compares update NZCV and are the three
    # complex SIMD/FP forms intentionally included in the control-flow lens.
    control_complex = {
        row["id"] for row in canonical
        if owners.get(row["id"]) == "complex_simd_fp" and mnemonic(row.get("assembly", "")) == "FCCMP"
    }
    if len(control_complex) != 3:
        raise RuntimeError(f"control complex census mismatch: {len(control_complex)} != 3")
    labels: dict[str, set[str]] = collections.defaultdict(set)
    for row_id in sorted(atomic_memory): labels[row_id].add("atomic-memory")
    for row_id in sorted(atomic_system): labels[row_id].add("atomic-memory")
    for row_id in sorted(control_general): labels[row_id].add("control-flow")
    for row_id in sorted(control_complex): labels[row_id].add("control-flow")
    census = {
        "atomic-memory": {"memory": len(atomic_memory), "system_barriers": len(atomic_system), "total": len(atomic_memory) + len(atomic_system)},
        "control-flow": {"general": len(control_general), "complex_simd_fp": len(control_complex), "total": len(control_general) + len(control_complex)},
    }
    return labels, census


def load_rows(canonical: Path) -> list[dict[str, Any]]:
    raw = canonical.read_bytes()
    if hashlib.sha256(raw).hexdigest() != CANONICAL_SHA256:
        raise RuntimeError("canonical JSONL SHA-256 does not match the pinned input")
    rows = [json.loads(line) for line in raw.decode().splitlines() if line.strip()]
    # UDF is architecturally an intentional undefined instruction, but it is
    # still a canonical M1 encoding row and belongs to the pinned 1,523-row
    # denominator.  The canonical importer records that status explicitly;
    # semantic metadata retains it rather than silently dropping the row.
    rows = [row for row in rows if row.get("apple_m1") and row.get("kind") in ("canonical", "alias")]
    rows.sort(key=lambda row: row["id"])
    if len(rows) != CANONICAL_COUNT or sum(row["kind"] == "canonical" for row in rows) != CANONICAL_DENOMINATOR or sum(row["kind"] == "alias" for row in rows) != ALIAS_DENOMINATOR:
        raise RuntimeError("pinned Apple-M1 denominator mismatch")
    if any(row.get("unresolved_mask") != "0x00000000" for row in rows if row.get("kind") == "canonical"):
        raise RuntimeError("canonical input contains unresolved instruction bits")
    return rows


def semantic_rows(rows: list[dict[str, Any]], source: Path) -> tuple[list[dict[str, Any]], dict[str, Any]]:
    encodings, explanation_index = source_index(source)
    numeric_source_keys = numeric_relation_source_keys(rows, explanation_index)
    numeric_source_signatures = [json.dumps(value, sort_keys=True, separators=(",", ":")) for value in numeric_source_keys]
    if len(set(numeric_source_signatures)) != len(numeric_source_signatures):
        raise RuntimeError("numeric relation source corpus contains duplicate row/operand keys")
    owners, owner_census = owner_sets([row for row in rows if row["kind"] == "canonical"])
    cross_labels, cross_census = cross_lens_sets(rows, owners)
    output = []
    counts = collections.Counter()
    mode_counts = collections.Counter()
    all_kinds = collections.Counter()
    all_transforms = collections.Counter()
    table_class_census = collections.Counter()
    parsed_vm_programs = 0
    residual_overlay_rows = 0
    for form_index, row in enumerate(rows):
        key = (row["iform_file"], row["encoding_name"])
        encoding = encodings.get(key)
        if encoding is None:
            raise RuntimeError(f"missing XML encoding for {row['id']}")
        anchors = anchor_info(encoding)
        explanations = explanation_index.get(key, [])
        canonical_fields = {field["name"] for field in row.get("fields", [])}
        canonical_field_widths = {
            field["name"]: sum(segment["width"] for segment in field.get("segments", []))
            for field in row.get("fields", [])
        }
        operands = []
        transforms = []
        for anchor in anchors:
            matching = [x for x in explanations if x.get("link") == anchor.get("link")]
            kinds, flags = classify(anchor.get("link") or "", anchor.get("symbol") or "", row.get("assembly", ""), matching)
            fields = []
            for explanation in matching:
                for field in fields_from_expr(explanation.get("encodedin"), canonical_fields)[0]:
                    if field not in fields:
                        fields.append(field)
            source_indices = [index for index, x in enumerate(explanations) if x.get("link") == anchor.get("link")]
            operand_transform_first = len(transforms)
            transforms.extend(transform_records({"form_id": row["id"], "link": anchor.get("link")}, matching,
                                                canonical_fields, canonical_field_widths, table_class_census))
            operand = {
                "position": anchor["position"],
                "link": anchor.get("link"),
                "base_link": base_link(anchor.get("link") or ""),
                "symbol": anchor.get("symbol"),
                "kinds": kinds,
                "flags": flags,
                # Role/direction require instruction-level dataflow, which is
                # intentionally outside this binding-only projection.  Keep
                # explicit UNKNOWN values rather than exposing substring
                # heuristics as authoritative semantics (stores read their
                # source registers, for example).
                "role": "unknown",
                "direction": "unknown",
                "classification_status": "presentation-only",
                "fields": fields,
                "explanation_indices": source_indices,
                "transform_first": operand_transform_first,
                "transform_count": len(transforms) - operand_transform_first,
                "register_widths": sorted({width for x in matching for width in register_widths(anchor.get("link") or "", anchor.get("symbol") or "", x.get("intro", ""))}),
                "simd_widths": sorted({width for x in matching for width in simd_widths(anchor.get("link") or "", x.get("intro", ""))}),
                "ranges": ranges_from_text(x.get("intro", "") for x in matching),
                "optional_default": "x30" if "defaults to x30" in " ".join(x.get("intro", "") for x in matching).lower() else None,
                "sp_legal": "sp_allowed" in flags,
                "zr_legal": "zr_allowed" in flags,
            }
            operands.append(operand)
            all_kinds.update(kinds)
        for transform in transforms:
            all_transforms[transform["kind"]] += 1
            if "program" in transform:
                parsed_vm_programs += 1
            if transform.get("kind") == "overlay_required":
                residual_overlay_rows += 1
        owner = owners.get(row["id"], "alias") if row["kind"] == "canonical" else "alias"
        mode = owner_mode(row, owner)
        counts[owner] += 1
        if mode: mode_counts[mode] += 1
        # Retain only compact source identity and bit/value maps.  The Arm
        # archive's narrative prose is deliberately not reproduced.
        evidence = []
        for evidence_index, explanation in enumerate(explanations):
            evidence.append({
                "xml_file": explanation["xml_file"],
                "link": explanation["link"],
                "kind": explanation["kind"],
                "source_index": evidence_index,
            })
        constraints = {
            "feature_tags": row.get("feature_tags", []),
            "program": constraint_program(row.get("constraints")),
        }
        aliases = {
            "kind": row["kind"],
            "target_file": row.get("alias_to", {}).get("file"),
            "target_id": row.get("alias_to", {}).get("id"),
            "target_encoding_id": row.get("alias_to", {}).get("encoding_id"),
            "condition_program": constraint_program(row.get("alias_condition")),
            "preference_condition_program": constraint_program(row.get("alias_preference_condition")),
            "preference_rank": row.get("alias_preference_rank"),
            "preferences": [{"rank": preference.get("rank"), "alias_file": preference.get("alias_file"), "alias_id": preference.get("alias_id"), "condition_program": constraint_program(preference.get("condition"))}
                            for preference in row.get("alias_preferences", [])],
        }
        digest = row.get("digest")
        proof = None
        binding_confidence = "mechanical" if anchors and all(op["fields"] or op["kinds"] == ["fixed_constant"] for op in operands) else ("explicit-overlay" if anchors else "fixed-constant")
        if row["id"] in SHARED_DECODE_PROOFS:
            shared = SHARED_DECODE_PROOFS[row["id"]]
            # The V_hv token is a field-free destination-width projection.
            # XML evidence supplies Rd/Rn/Q and the compact Q table; the
            # shared SD row plus llvm-mc word closes the otherwise synthetic
            # overlay without embedding source prose.
            transforms.append({
                "kind": "shared_decode",
                "source": 0,
                "program": [{"op": "shared_decode", "fields": ["Rd", "Rn", "Q"], "arrangements": [[0, "4H"], [1, "8H"]]}],
                "invertible": True,
                "shared_decode_id": shared["shared_decode_id"],
                "fields": ["Rd", "Rn", "Q"],
                "llvm_mc_word": shared["llvm_mc_word"],
            })
            for operand in operands:
                if operand.get("link") == "V_hv":
                    operand["transform_first"] = len(transforms) - 1
                    operand["transform_count"] = 1
            proof = {
                "kind": "shared_decode",
                "status": "proven",
                "shared_decode_id": shared["shared_decode_id"],
                "fields": ["Rd", "Rn", "Q"],
                "arrangement_values": [["0", "4H"], ["1", "8H"]],
                "llvm_mc_word": shared["llvm_mc_word"],
            }
            binding_confidence = "proven-overlay"
            all_transforms["shared_decode"] += 1
        output.append({
            "schema_version": SCHEMA_VERSION,
            "form_index": form_index,
            "id": row["id"],
            "source_digest": digest,
            "source_digest_algorithm": "canonical-row-digest-fnv1a64",
            "kind": row["kind"],
            "status": row.get("status", "defined"),
            "owner": owner,
            "owner_mode": mode,
            "cross_lenses": sorted(cross_labels.get(row["id"], set())),
            "mnemonic": mnemonic(row.get("assembly", "")),
            "encoding_name": row["encoding_name"],
            "assembly": row.get("assembly"),
            "fixed_mask": row.get("fixed_mask"),
            "fixed_value": row.get("fixed_value"),
            "field_mask": row.get("field_mask"),
            "fields": row.get("fields", []),
            "operands": operands,
            "transforms": transforms,
            "constraints": constraints,
            "evidence": evidence,
            "binding_proof": proof,
            "alias": aliases,
            "raw_layout_resolved": row.get("unresolved_mask") == "0x00000000",
            "binding_status": "binding_complete",
            "executable_semantics": False,
            "operand_classification_status": "presentation-only",
            "binding_confidence": binding_confidence,
            "binding_reason": "shared_decode_proof" if proof else ("xml_link_enclist" if anchors else "fixed_constant"),
            "provenance": {
                "canonical_sha256": CANONICAL_SHA256,
                "source_tree_sha256": SOURCE_TREE_SHA256,
                "xml_file": row["iform_file"],
                "xml_encoding": row["encoding_name"],
            },
        })
    if len(output) != CANONICAL_COUNT:
        raise RuntimeError("semantic row count mismatch")
    expected_modes = {"general/base": 144, "general/imm": 133, "general/reg": 18, "general/pre": 20, "general/post": 18, "FP/imm": 32, "FP/reg": 12, "FP/pre": 16, "FP/post": 16, "AdvSIMD-list/base": 50, "AdvSIMD-list/post": 100}
    if dict(mode_counts) != expected_modes:
        raise RuntimeError(f"memory mode census mismatch: {dict(mode_counts)} != {expected_modes}")
    canonical_counts = {key: value for key, value in counts.items() if key != "alias"}
    parsed_vm_programs = sum(1 for row in output for transform in row.get("transforms", []) if "program" in transform)
    residual_overlay_rows = sum(1 for row in output if any(transform.get("kind") == "overlay_required" for transform in row.get("transforms", [])))
    numeric_projected = []
    numeric_residual = []
    for row in output:
        for transform in row.get("transforms", []):
            relation_key = transform.get("relation_key")
            if relation_key is None:
                continue
            numeric_projected.append(relation_key)
            if transform.get("kind") == "overlay_required":
                numeric_residual.append(relation_key)
    numeric_projected_signatures = [json.dumps(value, sort_keys=True, separators=(",", ":")) for value in numeric_projected]
    numeric_residual_signatures = [json.dumps(value, sort_keys=True, separators=(",", ":")) for value in numeric_residual]
    if sorted(numeric_projected_signatures) != sorted(numeric_source_signatures):
        missing = sorted(set(numeric_source_signatures) - set(numeric_projected_signatures))
        extra = sorted(set(numeric_projected_signatures) - set(numeric_source_signatures))
        raise RuntimeError(f"numeric relation source/projection mismatch: missing={missing[:3]!r} extra={extra[:3]!r}")
    unknown_role_operands = sum(1 for row in output for operand in row.get("operands", []) if operand.get("role") == "unknown" or operand.get("direction") == "unknown")
    no_anchor = [row for row in output if not row["operands"]]
    proven = [row for row in output if row.get("binding_confidence") == "proven-overlay"]
    relation_digest = hashlib.sha256(("\n".join(sorted(numeric_source_signatures)) + "\n").encode()).hexdigest()
    for key in ("key_bitfield_cells", "key_non_bitfield_cells", "key_missing_class_cells", "key_fallback_cells",
                "inferred_width_columns", "key_inferred_width_cells", "numeric_10_11_cells",
                "bitfield_numeric_10_11_cells", "non_bitfield_numeric_10_11_cells"):
        table_class_census.setdefault(key, 0)
    return output, {"owners": owner_census, "owner_counts": dict(sorted(counts.items())), "canonical_owner_counts": dict(sorted(canonical_counts.items())), "memory_modes": dict(sorted(mode_counts.items())), "semantic_kinds": dict(sorted(all_kinds.items())), "transform_kinds": dict(sorted(all_transforms.items())), "parsed_vm_programs": parsed_vm_programs, "residual_overlay_rows": residual_overlay_rows, "unknown_role_operands": unknown_role_operands, "ambiguous_role_operands": 0, "cross_lenses": cross_census, "proven_overlay_rows": len(proven), "proven_overlay_ids": [row["id"] for row in proven], "no_anchor_rows": len(no_anchor), "no_anchor_canonical_rows": sum(row["kind"] == "canonical" for row in no_anchor), "no_anchor_alias_rows": sum(row["kind"] == "alias" for row in no_anchor), "numeric_relation_records": len(numeric_source_signatures), "numeric_relation_parsed": len(numeric_projected_signatures) - len(numeric_residual_signatures), "numeric_relation_residual": len(numeric_residual_signatures), "numeric_relation_source_key_sha256": relation_digest, "numeric_relation_source_keys": numeric_source_keys, "numeric_relation_projected_keys": numeric_projected, "numeric_relation_residual_keys": numeric_residual, "table_key_classification": dict(sorted(table_class_census.items()))}


def collect_strings(rows: list[dict[str, Any]]) -> tuple[dict[str, int], bytes]:
    values: set[str] = {""}
    def walk(value: Any) -> None:
        if isinstance(value, str): values.add(value)
        elif isinstance(value, list):
            for item in value: walk(item)
        elif isinstance(value, dict):
            if "program" in value:
                values.add(program_text(value.get("program")))
            for item in value.values(): walk(item)
    for row in rows: walk(row)
    ordered = sorted(values, key=lambda value: value.encode("utf-8"))
    pool = bytearray(); offsets: dict[str, int] = {}
    for value in ordered:
        offsets[value] = len(pool); pool.extend(value.encode("utf-8")); pool.append(0)
    return offsets, bytes(pool)


FORBIDDEN_PROJECTION_KEYS = {"intro", "hover", "encodedin", "feature_expression"}


def validate_compact_projection(rows: list[dict[str, Any]]) -> None:
    """Fail generation if source prose or raw XML expressions leak out."""
    def walk(value: Any, path: str) -> None:
        if isinstance(value, dict):
            for key, child in value.items():
                if key in FORBIDDEN_PROJECTION_KEYS:
                    raise RuntimeError(f"forbidden raw-source key in semantic projection: {path}.{key}")
                if key == "table":
                    raise RuntimeError(f"raw evidence table in semantic projection: {path}")
                walk(child, f"{path}.{key}")
        elif isinstance(value, list):
            for index, child in enumerate(value): walk(child, f"{path}[{index}]")
    for row in rows: walk(row, row.get("id", "row"))


def string_statistics(rows: list[dict[str, Any]]) -> tuple[int, list[dict[str, Any]]]:
    values: set[str] = set()
    def walk(value: Any) -> None:
        if isinstance(value, str): values.add(value)
        elif isinstance(value, list):
            for item in value: walk(item)
        elif isinstance(value, dict):
            for item in value.values(): walk(item)
    for row in rows: walk(row)
    ordered = sorted(values, key=lambda value: (-len(value.encode("utf-8")), value.encode("utf-8")))
    return sum(len(value.encode("utf-8")) + 1 for value in values), [{"bytes": len(value.encode("utf-8")), "value": value} for value in ordered[:10]]


def parse_hex(value: str | None) -> int:
    return int(value, 16) if value else 0



def generate_header(rows: list[dict[str, Any]], census: dict[str, Any], path: Path) -> tuple[int, str]:
    """Emit the pointer-free C projection used by the bounded runtime."""
    offsets, pool = collect_strings(rows)
    census["string_pool_bytes"] = len(pool)
    census["largest_strings"] = string_statistics(rows)[1]
    fields: list[dict[str, Any]] = []
    segments: list[dict[str, int]] = []
    operands: list[dict[str, Any]] = []
    operand_field_indices: list[int] = []
    transforms: list[dict[str, Any]] = []
    transform_parts: list[dict[str, int]] = []
    value_atoms: list[dict[str, Any]] = []
    value_entries: list[dict[str, int]] = []
    table_headers: list[dict[str, int]] = []
    table_key_headers: list[int] = []
    alias_descriptors: list[dict[str, Any]] = []
    alias_condition_tokens: list[int] = []
    alias_preference_condition_tokens: list[int] = []
    alias_preferences: list[dict[str, Any]] = []
    alias_form_ids: list[int] = []
    constraint_descriptors: list[dict[str, Any]] = []
    constraint_feature_tags: list[int] = []
    constraint_program_tokens: list[int] = []
    program_instructions: list[dict[str, Any]] = []
    program_operands: list[dict[str, Any]] = []
    forms: list[dict[str, Any]] = []
    owner_values = {"fixed32": 0, "direct_gpr": 1, "scalar_integer": 2, "direct_simd": 3, "system": 4, "memory": 5, "general_nonmemory": 6, "complex_simd_fp": 7, "alias": 8}
    kind_values = {"gpr_register": 0, "gpr_width_selector": 1, "simd_register": 2, "simd_arrangement": 3, "simd_width_selector": 4, "simd_list": 5, "simd_lane": 6, "integer_immediate": 7, "fp_immediate": 8, "condition": 9, "nzcv_flags": 10, "shift": 11, "extend": 12, "rotate": 13, "memory_base": 14, "memory_offset": 15, "memory_data_register": 16, "label_fixup": 17, "system_register": 18, "system_operation": 19, "barrier_option": 20, "prefetch_operation": 21, "fixed_constant": 22, "other": 23, "simd_prefix_selector": 24}
    transform_values = {"concat": 0, "slice": 1, "integer_decode": 2, "text_transform": 3, "value_table": 4, "overlay_required": 5, "shared_decode": 6}
    status_values = {"defined": 0, "undefined": 1}

    no_program = 0xffffffff
    no_field = 0xffffffff
    no_slice = 0xffff

    def append_program(program: list[dict[str, Any]] | None, field_defs: dict[str, int]) -> tuple[int, int]:
        """Flatten one normalized VM program into typed, bounded records."""
        if not program:
            return no_program, 0
        if not isinstance(program, list):
            raise RuntimeError(f"normalized VM program is not a list: {program!r}")
        first = len(program_instructions)
        for operation in program:
            if not isinstance(operation, dict) or operation.get("op") not in PROGRAM_OP_VALUES:
                raise RuntimeError(f"unsupported normalized VM operation: {operation!r}")
            op_name = operation["op"]
            instruction = {
                "op": PROGRAM_OP_VALUES[op_name], "field_offset": no_field,
                "text_offset": offsets[""], "operand_first": 0,
                "operand_count": 0, "value": 0, "high": no_slice,
                "low": no_slice, "width": 0, "modulus": 0,
            }
            if op_name == "field":
                name = operation.get("name")
                if not isinstance(name, str) or name not in field_defs:
                    raise RuntimeError(f"VM field is not bound to this form: {operation!r}")
                instruction["field_offset"] = offsets[name]
                if "high" in operation or "low" in operation:
                    if not isinstance(operation.get("high"), int) or not isinstance(operation.get("low"), int):
                        raise RuntimeError(f"VM field slice is malformed: {operation!r}")
                    instruction["high"] = int(operation["high"])
                    instruction["low"] = int(operation["low"])
                    if instruction["low"] < 0 or instruction["high"] < instruction["low"] or instruction["high"] >= field_defs[name]:
                        raise RuntimeError(f"VM field slice exceeds this form's field width: {operation!r}")
                    instruction["width"] = instruction["high"] - instruction["low"] + 1
            elif op_name == "uint_concat":
                parts = operation.get("parts")
                if not isinstance(parts, list) or not parts:
                    raise RuntimeError(f"VM uint_concat has no parts: {operation!r}")
                operand_first = len(program_operands)
                for part in parts:
                    if not isinstance(part, dict) or part.get("op") not in {"field", "literal"}:
                        raise RuntimeError(f"VM uint_concat part is not a field/literal: {operation!r}")
                    if part.get("op") == "literal":
                        program_operands.append({"kind": PROGRAM_OPERAND_KIND_VALUES["literal"], "field_offset": no_field,
                                                 "text_offset": offsets[""], "value": int(part.get("value", 0)), "high": no_slice, "low": no_slice, "width": 0})
                        continue
                    name = part.get("name")
                    if not isinstance(name, str) or name not in field_defs:
                        raise RuntimeError(f"VM concat field is not bound to this form: {part!r}")
                    high = int(part["high"]) if "high" in part else no_slice
                    low = int(part["low"]) if "low" in part else no_slice
                    if high != no_slice and (low < 0 or high < low or high >= field_defs[name]):
                        raise RuntimeError(f"VM concat field slice exceeds this form's field width: {part!r}")
                    width = high - low + 1 if high != no_slice and low != no_slice else 0
                    program_operands.append({"kind": PROGRAM_OPERAND_KIND_VALUES["field"], "field_offset": offsets[name],
                                             "text_offset": offsets[""], "value": 0, "high": high, "low": low, "width": width})
                instruction["operand_first"] = operand_first
                instruction["operand_count"] = len(program_operands) - operand_first
            elif op_name in {"sign_extend"}:
                instruction["width"] = int(operation.get("bits", 0))
            elif op_name in {"scale_mul", "scale_div", "scale_pow2", "add_const", "sub_from_const", "literal"}:
                instruction["value"] = int(operation.get("value", 0))
            elif op_name == "register_add_mod":
                name = operation.get("field")
                if not isinstance(name, str) or name not in field_defs:
                    raise RuntimeError(f"VM register field is not bound to this form: {operation!r}")
                instruction["field_offset"] = offsets[name]
                instruction["value"] = int(operation.get("delta", 0))
                instruction["modulus"] = int(operation.get("modulus", 0))
            elif op_name == "text_factor":
                value = operation.get("value")
                if not isinstance(value, str) or value not in offsets:
                    raise RuntimeError(f"VM text factor is missing from string pool: {operation!r}")
                instruction["text_offset"] = offsets[value]
            elif op_name == "shared_decode":
                fields = operation.get("fields")
                arrangements = operation.get("arrangements")
                if not isinstance(fields, list) or not isinstance(arrangements, list):
                    raise RuntimeError(f"VM shared decode is malformed: {operation!r}")
                operand_first = len(program_operands)
                for name in fields:
                    if not isinstance(name, str) or name not in field_defs:
                        raise RuntimeError(f"VM shared-decode field is not bound to this form: {name!r}")
                    program_operands.append({"kind": PROGRAM_OPERAND_KIND_VALUES["field"], "field_offset": offsets[name],
                                             "text_offset": offsets[""], "value": 0, "high": no_slice, "low": no_slice, "width": 0})
                for arrangement in arrangements:
                    if not isinstance(arrangement, (list, tuple)) or len(arrangement) != 2:
                        raise RuntimeError(f"VM shared-decode arrangement is malformed: {arrangement!r}")
                    selector, value = arrangement
                    if not isinstance(selector, int) or not isinstance(value, str) or value not in offsets:
                        raise RuntimeError(f"VM shared-decode arrangement is malformed: {arrangement!r}")
                    program_operands.append({"kind": PROGRAM_OPERAND_KIND_VALUES["arrangement"], "field_offset": no_field,
                                             "text_offset": offsets[value], "value": selector, "high": no_slice, "low": no_slice, "width": 0})
                instruction["operand_first"] = operand_first
                instruction["operand_count"] = len(program_operands) - operand_first
            program_instructions.append(instruction)
        return first, len(program_instructions) - first

    def append_atom(value: dict[str, Any], field_defs: dict[str, int]) -> dict[str, Any]:
        kind = value.get("type")
        if kind == "integer":
            return {"type": 0, "text_offset": offsets[""], "integer": int(value["value"]), "program_first": no_program, "program_count": 0}
        type_values = {"bits": 1, "enum": 2, "expression": 3, "program": 4}
        if kind == "program":
            serialized = program_text(value.get("program"))
            if serialized not in offsets:
                raise RuntimeError(f"normalized VM program missing from string pool: {value!r}")
            program_first, program_count = append_program(value.get("program"), field_defs)
            return {"type": type_values[kind], "text_offset": offsets[serialized], "integer": 0,
                    "program_first": program_first, "program_count": program_count}
        if kind not in type_values or not isinstance(value.get("value"), str):
            raise RuntimeError(f"invalid normalized value atom: {value!r}")
        if value["value"] not in offsets:
            raise RuntimeError(f"normalized value missing from string pool: {value!r}")
        return {"type": type_values[kind], "text_offset": offsets[value["value"]], "integer": 0,
                "program_first": no_program, "program_count": 0}

    def append_table(transform: dict[str, Any], field_defs: dict[str, int]) -> tuple[int, int]:
        first = len(value_entries)
        entries = transform.get("entries", [])
        if transform.get("key_arity", 0) <= 0 or transform.get("result_arity") != 1:
            raise RuntimeError(f"invalid value-table arity: {transform!r}")
        for entry in entries:
            key = entry.get("key", [])
            result = entry.get("result", [])
            if len(key) != transform["key_arity"] or len(result) != 1:
                raise RuntimeError(f"value-table key/result arity mismatch: {entry!r}")
            key_first = len(value_atoms)
            value_atoms.extend(append_atom(atom, field_defs) for atom in key)
            result_first = len(value_atoms)
            value_atoms.extend(append_atom(atom, field_defs) for atom in result)
            value_entries.append({"key_first": key_first, "result_first": result_first, "key_count": len(key), "result_count": 1})
        return first, len(entries)

    for form in rows:
        constraint_feature_first = len(constraint_feature_tags)
        for tag in form.get("constraints", {}).get("feature_tags", []):
            if tag not in offsets:
                raise RuntimeError(f"constraint feature tag missing from string pool: {tag!r}")
            constraint_feature_tags.append(offsets[tag])
        constraint_program_first = len(constraint_program_tokens)
        for token in form.get("constraints", {}).get("program", []):
            if token not in offsets:
                raise RuntimeError(f"constraint token missing from string pool: {token!r}")
            constraint_program_tokens.append(offsets[token])
        constraint_descriptors.append({
            "feature_first": constraint_feature_first,
            "feature_count": len(constraint_feature_tags) - constraint_feature_first,
            "program_first": constraint_program_first,
            "program_count": len(constraint_program_tokens) - constraint_program_first,
        })
        alias = form.get("alias", {})
        alias_condition_first = len(alias_condition_tokens)
        for token in alias.get("condition_program", []):
            if token not in offsets:
                raise RuntimeError(f"alias condition token missing from string pool: {token!r}")
            alias_condition_tokens.append(offsets[token])
        alias_preference_condition_first = len(alias_preference_condition_tokens)
        for token in alias.get("preference_condition_program", []):
            if token not in offsets:
                raise RuntimeError(f"alias preference condition token missing from string pool: {token!r}")
            alias_preference_condition_tokens.append(offsets[token])
        alias_preference_condition_count = len(alias_preference_condition_tokens) - alias_preference_condition_first
        alias_preference_first = len(alias_preferences)
        for preference in alias.get("preferences", []):
            preference_condition_first = len(alias_preference_condition_tokens)
            for token in preference.get("condition_program", []):
                if token not in offsets:
                    raise RuntimeError(f"alias preference token missing from string pool: {token!r}")
                alias_preference_condition_tokens.append(offsets[token])
            alias_preferences.append({
                "alias_file": offsets[preference.get("alias_file") or ""],
                "alias_id": offsets[preference.get("alias_id") or ""],
                "condition_first": preference_condition_first,
                "condition_count": len(alias_preference_condition_tokens) - preference_condition_first,
                "rank": preference.get("rank") if preference.get("rank") is not None else -2147483648,
            })
        alias_descriptors.append({
            "target_file": offsets[alias.get("target_file") or ""],
            "target_id": offsets[alias.get("target_id") or ""],
            "target_encoding_id": offsets[alias.get("target_encoding_id") or ""],
            "condition_first": alias_condition_first,
            "condition_count": len(alias_condition_tokens) - alias_condition_first,
            "preference_condition_first": alias_preference_condition_first,
            "preference_condition_count": alias_preference_condition_count,
            "preference_rank": alias.get("preference_rank") if alias.get("preference_rank") is not None else -2147483648,
            "preference_first": alias_preference_first,
            "preference_count": len(alias_preferences) - alias_preference_first,
        })
        if form.get("kind") == "alias":
            alias_form_ids.append(form["form_index"])
        form_field_first = len(fields)
        field_ids: dict[str, int] = {}
        for field in form.get("fields", []):
            name = field["name"]
            if name in field_ids:
                raise RuntimeError(f"duplicate field in form {form['id']}: {name}")
            source_mask = 0
            segment_first = len(segments)
            for segment in field.get("segments", []):
                width = segment["width"]
                source_mask |= ((1 << width) - 1) << segment["value_lsb"]
                segments.append({"instruction_lsb": segment["instruction_lsb"], "width": width, "value_lsb": segment["value_lsb"]})
            field_ids[name] = len(fields)
            fields.append({"name": name, "source_mask": source_mask, "segment_first": segment_first, "segment_count": len(field.get("segments", [])), "width": sum(x["width"] for x in field.get("segments", []))})
        field_defs = {field["name"]: field["width"] for field in fields[form_field_first:]}
        form_operand_first = len(operands)
        form_transform_first = len(transforms)
        for operand in form.get("operands", []):
            operand_transform_first = len(transforms)
            start = operand.get("transform_first", 0)
            end = start + operand.get("transform_count", 0)
            for transform in form.get("transforms", [])[start:end]:
                parts = transform.get("parts", [])
                part_first = len(transform_parts)
                for part in parts:
                    if not isinstance(part, str) or part not in offsets:
                        raise RuntimeError(f"unknown concat part in {form['id']}: {part!r}")
                    transform_parts.append({"offset": offsets[part]})
                value_first, value_count = append_table(transform, field_defs) if transform.get("kind") == "value_table" else (0, 0)
                table_id = 0xffffffff
                if transform.get("kind") == "value_table":
                    table_id = len(table_headers)
                    key_headers = transform.get("key_headers", [])
                    if not key_headers or transform.get("result_header") not in offsets:
                        raise RuntimeError(f"value-table header missing in {form['id']}: {transform!r}")
                    key_first = len(table_key_headers)
                    for header in key_headers:
                        if header not in offsets:
                            raise RuntimeError(f"value-table key header missing from string pool: {header!r}")
                        table_key_headers.append(offsets[header])
                    table_headers.append({"key_header_first": key_first, "key_header_count": len(key_headers), "result_header": offsets[transform["result_header"]]})
                kind = transform_values.get(transform.get("kind"))
                if kind is None:
                    raise RuntimeError(f"unknown transform kind in {form['id']}: {transform.get('kind')!r}")
                program_first, program_count = append_program(transform.get("program"), field_defs) if "program" in transform else (no_program, 0)
                transforms.append({"kind": kind, "source": transform.get("source", 0), "p0": transform.get("high", 0) if isinstance(transform.get("high", 0), int) else 0, "p1": transform.get("low", 0) if isinstance(transform.get("low", 0), int) else 0, "table_id": table_id, "expression": program_text(transform.get("program")) if "program" in transform else transform.get("expression", ""), "program_first": program_first, "program_count": program_count, "part_first": part_first, "part_count": len(parts), "value_first": value_first, "value_count": value_count, "invertible": bool(transform.get("invertible"))})
            kinds = operand.get("kinds", ["other"])
            kind_mask = 0
            for name in kinds:
                if name not in kind_values:
                    raise RuntimeError(f"unknown operand kind in {form['id']}: {name!r}")
                kind_mask |= 1 << kind_values[name]
            references = operand.get("fields", [])
            if len(set(references)) != len(references):
                raise RuntimeError(f"duplicate operand field reference in {form['id']}: {references!r}")
            field_index_first = len(operand_field_indices)
            for name in references:
                if name not in field_ids:
                    raise RuntimeError(f"operand field order/name mismatch in {form['id']}: {name!r}")
                operand_field_indices.append(field_ids[name])
            flags = 0
            for name in operand.get("flags", []):
                bit = SEMANTIC_FLAG_BITS.get(name)
                if bit is None or bit >= 64:
                    raise RuntimeError(f"unknown/overflow operand flag: {name!r}")
                flags |= 1 << bit
            classification_status = operand.get("classification_status", "presentation-only")
            if classification_status not in CLASSIFICATION_STATUS_VALUES:
                raise RuntimeError(f"unknown operand classification status in {form['id']}: {classification_status!r}")
            operands.append({"form": form["form_index"], "position": operand["position"], "link": operand.get("link") or "", "symbol": operand.get("symbol") or "", "field_first": field_ids[references[0]] if references else 0xffffffff, "field_index_first": field_index_first, "field_count": len(references), "field_index_count": len(references), "transform_first": operand_transform_first, "transform_count": len(transforms) - operand_transform_first, "kind": kind_values[kinds[0]] if kinds else 23, "kind_mask": kind_mask, "flags": flags, "classification_status": CLASSIFICATION_STATUS_VALUES[classification_status], "role": operand.get("role") or "", "direction": operand.get("direction") or ""})
        forms.append({"id": form["form_index"], "source_digest": parse_hex(form.get("source_digest")), "name": form["id"], "mnemonic": form.get("mnemonic") or "", "assembly": form.get("assembly") or "", "owner": owner_values.get(form.get("owner"), 8), "kind": 0 if form.get("kind") == "canonical" else 1, "status": status_values.get(form.get("status"), 0), "fixed_mask": parse_hex(form.get("fixed_mask")), "fixed_value": parse_hex(form.get("fixed_value")), "field_first": form_field_first, "operand_first": form_operand_first, "transform_first": form_transform_first, "field_count": len(form.get("fields", [])), "operand_count": len(form.get("operands", [])), "transform_count": len(transforms) - form_transform_first, "raw_layout_resolved": bool(form.get("raw_layout_resolved"))})

    typed_transform_program_count = sum(1 for transform in transforms if transform["program_count"] != 0)
    typed_value_program_count = sum(1 for atom in value_atoms if atom["program_count"] != 0)
    if typed_transform_program_count != census.get("parsed_vm_programs", 0):
        raise RuntimeError(f"typed transform-program census mismatch: {typed_transform_program_count} != {census.get('parsed_vm_programs', 0)}")
    if typed_transform_program_count != 372 or typed_value_program_count != 296:
        raise RuntimeError(f"typed program census mismatch: transforms={typed_transform_program_count}, value_atoms={typed_value_program_count}")
    census["typed_program_instructions"] = len(program_instructions)
    census["typed_program_operands"] = len(program_operands)
    census["typed_transform_programs"] = typed_transform_program_count
    census["typed_value_program_atoms"] = typed_value_program_count

    def c_string(value: str) -> str:
        result = ['"']
        for byte in value.encode("latin1"):
            if byte == 0x22: result.append('\\"')
            elif byte == 0x5c: result.append('\\\\')
            elif 0x20 <= byte < 0x7f: result.append(chr(byte))
            else: result.append(f"\\{byte:03o}")
        result.append('"')
        return "".join(result)

    def operand_blob() -> bytes:
        payload = bytearray()
        for item in operands:
            payload.extend(struct.pack("<7IQ3H4B2I", item["form"], offsets[item["link"]], offsets[item["symbol"]], item["field_first"], item["field_index_first"], item["transform_first"], item["kind_mask"], item["flags"], item["field_count"], item["field_index_count"], item["transform_count"], item["kind"], item["position"], item["classification_status"], 0, offsets[item["role"]], offsets[item["direction"]]))
        return bytes(payload)

    operand_blob_b64 = base64.b64encode(operand_blob()).decode("ascii")

    def segment_blob() -> bytes:
        return b"".join(struct.pack("<4B", item["instruction_lsb"], item["width"], item["value_lsb"], 0) for item in segments)

    def field_blob() -> bytes:
        return b"".join(struct.pack("<IIIHBx", offsets[item["name"]], item["source_mask"], item["segment_first"], item["segment_count"], item["width"]) for item in fields)

    def value_atom_blob() -> bytes:
        # Keep the logical value-atom shape in the generated header, but emit
        # the 20-byte little-endian wire record as a base64 blob so the C
        # frontend does not build an AST for 20k aggregate initializers.
        return b"".join(struct.pack("<IqIHBx", item["text_offset"], item["integer"], item["program_first"], item["program_count"], item["type"]) for item in value_atoms)

    def value_entry_blob() -> bytes:
        return b"".join(struct.pack("<IIHH", item["key_first"], item["result_first"], item["key_count"], item["result_count"]) for item in value_entries)

    segment_blob_b64 = base64.b64encode(segment_blob()).decode("ascii")
    field_blob_b64 = base64.b64encode(field_blob()).decode("ascii")
    value_atom_blob_b64 = base64.b64encode(value_atom_blob()).decode("ascii")
    value_entry_blob_b64 = base64.b64encode(value_entry_blob()).decode("ascii")
    chunks = [pool[index:index + 3900] for index in range(0, len(pool), 3900)] or [b""]
    lines = [
        "/* Generated by arm_a64_semantic_generate.py; do not edit. */", "#ifndef BUSTER_AARCH64_SEMANTIC_GENERATED_H", "#define BUSTER_AARCH64_SEMANTIC_GENERATED_H", "#include <buster/lib/base.h>", "",
        f"#define BUSTER_AARCH64_SEMANTIC_SCHEMA_VERSION {SCHEMA_VERSION}u", f"#define BUSTER_AARCH64_SEMANTIC_FORM_COUNT {len(forms)}u", f"#define BUSTER_AARCH64_SEMANTIC_FIELD_COUNT {len(fields)}u", f"#define BUSTER_AARCH64_SEMANTIC_SEGMENT_COUNT {len(segments)}u", f"#define BUSTER_AARCH64_SEMANTIC_FIELD_RECORD_BYTES 16u", f"#define BUSTER_AARCH64_SEMANTIC_FIELD_BLOB_SIZE {len(field_blob())}u", f"#define BUSTER_AARCH64_SEMANTIC_SEGMENT_RECORD_BYTES 4u", f"#define BUSTER_AARCH64_SEMANTIC_SEGMENT_BLOB_SIZE {len(segment_blob())}u", f"#define BUSTER_AARCH64_SEMANTIC_OPERAND_COUNT {len(operands)}u", f"#define BUSTER_AARCH64_SEMANTIC_OPERAND_FIELD_INDEX_COUNT {len(operand_field_indices)}u", f"#define BUSTER_AARCH64_SEMANTIC_OPERAND_RECORD_BYTES 54u", f"#define BUSTER_AARCH64_SEMANTIC_OPERAND_BLOB_SIZE {len(operand_blob())}u", f"#define BUSTER_AARCH64_SEMANTIC_TRANSFORM_COUNT {len(transforms)}u", f"#define BUSTER_AARCH64_SEMANTIC_TRANSFORM_PART_COUNT {len(transform_parts)}u", f"#define BUSTER_AARCH64_SEMANTIC_PROGRAM_INSTRUCTION_COUNT {len(program_instructions)}u", f"#define BUSTER_AARCH64_SEMANTIC_PROGRAM_OPERAND_COUNT {len(program_operands)}u", f"#define BUSTER_AARCH64_SEMANTIC_PARSED_PROGRAM_COUNT {typed_transform_program_count}u", f"#define BUSTER_AARCH64_SEMANTIC_VALUE_PROGRAM_COUNT {typed_value_program_count}u", f"#define BUSTER_AARCH64_SEMANTIC_VALUE_ENTRY_COUNT {len(value_entries)}u", f"#define BUSTER_AARCH64_SEMANTIC_VALUE_ATOM_COUNT {len(value_atoms)}u", f"#define BUSTER_AARCH64_SEMANTIC_TABLE_COUNT {len(table_headers)}u", f"#define BUSTER_AARCH64_SEMANTIC_TABLE_KEY_HEADER_COUNT {len(table_key_headers)}u", f"#define BUSTER_AARCH64_SEMANTIC_ALIAS_COUNT {len(alias_form_ids)}u", f"#define BUSTER_AARCH64_SEMANTIC_ALIAS_CONDITION_TOKEN_COUNT {len(alias_condition_tokens)}u", f"#define BUSTER_AARCH64_SEMANTIC_ALIAS_PREFERENCE_CONDITION_TOKEN_COUNT {len(alias_preference_condition_tokens)}u", f"#define BUSTER_AARCH64_SEMANTIC_ALIAS_PREFERENCE_COUNT {len(alias_preferences)}u", f"#define BUSTER_AARCH64_SEMANTIC_CONSTRAINT_COUNT {len(constraint_descriptors)}u", f"#define BUSTER_AARCH64_SEMANTIC_CONSTRAINT_FEATURE_TAG_COUNT {len(constraint_feature_tags)}u", f"#define BUSTER_AARCH64_SEMANTIC_CONSTRAINT_PROGRAM_TOKEN_COUNT {len(constraint_program_tokens)}u", f"#define BUSTER_AARCH64_SEMANTIC_STRING_POOL_SIZE {len(pool)}u", "",
        f"#define BUSTER_AARCH64_SEMANTIC_VALUE_ATOM_RECORD_BYTES 20u", f"#define BUSTER_AARCH64_SEMANTIC_VALUE_ATOM_BLOB_SIZE {len(value_atom_blob())}u", f"#define BUSTER_AARCH64_SEMANTIC_VALUE_ENTRY_RECORD_BYTES 12u", f"#define BUSTER_AARCH64_SEMANTIC_VALUE_ENTRY_BLOB_SIZE {len(value_entry_blob())}u",
        "typedef struct BusterA64SemanticGeneratedSegment BusterA64SemanticGeneratedSegment; struct BusterA64SemanticGeneratedSegment { u8 instruction_lsb; u8 width; u8 value_lsb; u8 reserved; };",
        "typedef struct BusterA64SemanticGeneratedField BusterA64SemanticGeneratedField; struct BusterA64SemanticGeneratedField { u32 name_offset; u32 source_mask; u32 segment_first; u16 segment_count; u8 width; u8 reserved; };",
        "typedef struct BusterA64SemanticGeneratedOperand BusterA64SemanticGeneratedOperand; struct BusterA64SemanticGeneratedOperand { u32 form_id; u32 link_offset; u32 symbol_offset; u32 field_first; u32 field_index_first; u32 transform_first; u32 kind_mask; u64 flags; u16 field_count; u16 field_index_count; u16 transform_count; u8 kind; u8 position; u8 classification_status; u8 reserved; u32 role_offset; u32 direction_offset; };",
        "typedef struct BusterA64SemanticGeneratedTransformPart BusterA64SemanticGeneratedTransformPart; struct BusterA64SemanticGeneratedTransformPart { u32 offset; };",
        "typedef struct BusterA64SemanticGeneratedValueAtom BusterA64SemanticGeneratedValueAtom; struct BusterA64SemanticGeneratedValueAtom { u32 text_offset; s64 integer; u32 program_first; u16 program_count; u8 type; u8 reserved[1]; };",
        "typedef struct BusterA64SemanticGeneratedValueEntry BusterA64SemanticGeneratedValueEntry; struct BusterA64SemanticGeneratedValueEntry { u32 key_first; u32 result_first; u16 key_count; u16 result_count; };",
        "typedef struct BusterA64SemanticGeneratedProgramInstruction BusterA64SemanticGeneratedProgramInstruction; struct BusterA64SemanticGeneratedProgramInstruction { u32 field_offset; u32 text_offset; u32 operand_first; s32 value; u16 operand_count; u16 high; u16 low; u16 width; u16 modulus; u8 op; u8 reserved[3]; };",
        "typedef struct BusterA64SemanticGeneratedProgramOperand BusterA64SemanticGeneratedProgramOperand; struct BusterA64SemanticGeneratedProgramOperand { u32 field_offset; u32 text_offset; s32 value; u16 high; u16 low; u16 width; u8 kind; u8 reserved; };",
        "typedef struct BusterA64SemanticGeneratedTransform BusterA64SemanticGeneratedTransform; struct BusterA64SemanticGeneratedTransform { u32 expression_offset; u32 source; u32 p0; u32 p1; u32 table_id; u32 program_first; u32 part_first; u32 value_first; u16 program_count; u16 part_count; u16 value_count; u8 kind; u8 invertible; u16 reserved; };",
        "typedef struct BusterA64SemanticGeneratedTableHeader BusterA64SemanticGeneratedTableHeader; struct BusterA64SemanticGeneratedTableHeader { u32 key_header_first; u16 key_header_count; u16 reserved; u32 result_header_offset; };",
        "typedef struct BusterA64SemanticGeneratedAlias BusterA64SemanticGeneratedAlias; struct BusterA64SemanticGeneratedAlias { u32 target_file_offset; u32 target_id_offset; u32 target_encoding_id_offset; u32 condition_first; u32 preference_condition_first; u32 preference_first; u16 condition_count; u16 preference_condition_count; u16 preference_count; u16 reserved; s32 preference_rank; };",
        "typedef struct BusterA64SemanticGeneratedAliasPreference BusterA64SemanticGeneratedAliasPreference; struct BusterA64SemanticGeneratedAliasPreference { u32 alias_file_offset; u32 alias_id_offset; u32 condition_first; u16 condition_count; u16 reserved; s32 rank; };",
        "typedef struct BusterA64SemanticGeneratedConstraint BusterA64SemanticGeneratedConstraint; struct BusterA64SemanticGeneratedConstraint { u32 feature_first; u32 program_first; u16 feature_count; u16 program_count; };",
        "typedef struct BusterA64SemanticGeneratedForm BusterA64SemanticGeneratedForm; struct BusterA64SemanticGeneratedForm { u32 name_offset; u32 mnemonic_offset; u32 assembly_offset; u64 source_digest; u32 fixed_mask; u32 fixed_value; u32 field_first; u32 operand_first; u32 transform_first; u16 field_count; u16 operand_count; u16 transform_count; u8 owner; u8 kind; u8 raw_layout_resolved; u8 status; };", "",
    ]
    lines.append("static const char8 buster_a64_semantic_string_pool[] =")
    lines.extend("    " + c_string(chunk.decode("latin1")) for chunk in chunks)
    lines.append("    ;")
    lines.append("static const char8 buster_a64_semantic_segment_blob[] =")
    lines.extend("    " + c_string(segment_blob_b64[index:index + 3900]) for index in range(0, len(segment_blob_b64), 3900))
    lines.append("    ;")
    lines.append("static const char8 buster_a64_semantic_field_blob[] =")
    lines.extend("    " + c_string(field_blob_b64[index:index + 3900]) for index in range(0, len(field_blob_b64), 3900))
    lines.append("    ;")
    lines.append("static const char8 buster_a64_semantic_operand_blob[] =")
    lines.extend("    " + c_string(operand_blob_b64[index:index + 3900]) for index in range(0, len(operand_blob_b64), 3900))
    lines.append("    ;")
    lines.append(f"static const u32 buster_a64_semantic_operand_field_indices[{max(1, len(operand_field_indices))}] = {{")
    lines.extend(f"    {value}," for value in operand_field_indices)
    if not operand_field_indices: lines.append("    0,")
    lines.append("};")
    lines.append(f"static const BusterA64SemanticGeneratedTransformPart buster_a64_semantic_transform_parts[{max(1, len(transform_parts))}] = {{")
    lines.extend(f"    {{{x['offset']}}}," for x in transform_parts)
    if not transform_parts: lines.append("    {0},")
    lines.append("};")
    lines.append(f"static const BusterA64SemanticGeneratedProgramInstruction buster_a64_semantic_program_instructions[{max(1, len(program_instructions))}] = {{")
    lines.extend(f"    {{{x['field_offset']}, {x['text_offset']}, {x['operand_first']}, {x['value']}, {x['operand_count']}, {x['high']}, {x['low']}, {x['width']}, {x['modulus']}, {x['op']}, {{0}}}}," for x in program_instructions)
    if not program_instructions: lines.append("    {0},")
    lines.append("};")
    lines.append(f"static const BusterA64SemanticGeneratedProgramOperand buster_a64_semantic_program_operands[{max(1, len(program_operands))}] = {{")
    lines.extend(f"    {{{x['field_offset']}, {x['text_offset']}, {x['value']}, {x['high']}, {x['low']}, {x['width']}, {x['kind']}, 0}}," for x in program_operands)
    if not program_operands: lines.append("    {0},")
    lines.append("};")
    lines.append("static const char8 buster_a64_semantic_value_atom_blob[] =")
    lines.extend("    " + c_string(value_atom_blob_b64[index:index + 3900]) for index in range(0, len(value_atom_blob_b64), 3900))
    lines.append("    ;")
    lines.append("static const char8 buster_a64_semantic_value_entry_blob[] =")
    lines.extend("    " + c_string(value_entry_blob_b64[index:index + 3900]) for index in range(0, len(value_entry_blob_b64), 3900))
    lines.append("    ;")
    lines.append(f"static const BusterA64SemanticGeneratedTableHeader buster_a64_semantic_table_headers[{max(1, len(table_headers))}] = {{")
    lines.extend(f"    {{{x['key_header_first']}, {x['key_header_count']}, 0, {x['result_header']}}}," for x in table_headers)
    if not table_headers: lines.append("    {0, 0, 0, 0},")
    lines.append("};")
    lines.append(f"static const u32 buster_a64_semantic_table_key_headers[{max(1, len(table_key_headers))}] = {{")
    lines.extend(f"    {value}," for value in table_key_headers)
    if not table_key_headers: lines.append("    0,")
    lines.append("};")
    lines.append(f"static const BusterA64SemanticGeneratedAlias buster_a64_semantic_aliases[{max(1, len(alias_descriptors))}] = {{")
    lines.extend(f"    {{{x['target_file']}, {x['target_id']}, {x['target_encoding_id']}, {x['condition_first']}, {x['preference_condition_first']}, {x['preference_first']}, {x['condition_count']}, {x['preference_condition_count']}, {x['preference_count']}, 0, {x['preference_rank']}}}," for x in alias_descriptors)
    if not alias_descriptors: lines.append("    {0},")
    lines.append("};")
    lines.append(f"static const u32 buster_a64_semantic_alias_condition_tokens[{max(1, len(alias_condition_tokens))}] = {{")
    lines.extend(f"    {value}," for value in alias_condition_tokens)
    if not alias_condition_tokens: lines.append("    0,")
    lines.append("};")
    lines.append(f"static const u32 buster_a64_semantic_alias_preference_condition_tokens[{max(1, len(alias_preference_condition_tokens))}] = {{")
    lines.extend(f"    {value}," for value in alias_preference_condition_tokens)
    if not alias_preference_condition_tokens: lines.append("    0,")
    lines.append("};")
    lines.append(f"static const BusterA64SemanticGeneratedAliasPreference buster_a64_semantic_alias_preferences[{max(1, len(alias_preferences))}] = {{")
    lines.extend(f"    {{{x['alias_file']}, {x['alias_id']}, {x['condition_first']}, {x['condition_count']}, 0, {x['rank']}}}," for x in alias_preferences)
    if not alias_preferences: lines.append("    {0},")
    lines.append("};")
    lines.append(f"static const u32 buster_a64_semantic_alias_form_ids[{max(1, len(alias_form_ids))}] = {{")
    lines.extend(f"    {value}," for value in alias_form_ids)
    if not alias_form_ids: lines.append("    0,")
    lines.append("};")
    lines.append(f"static const BusterA64SemanticGeneratedConstraint buster_a64_semantic_constraints[{max(1, len(constraint_descriptors))}] = {{")
    lines.extend(f"    {{{x['feature_first']}, {x['program_first']}, {x['feature_count']}, {x['program_count']}}}," for x in constraint_descriptors)
    if not constraint_descriptors: lines.append("    {0},")
    lines.append("};")
    lines.append(f"static const u32 buster_a64_semantic_constraint_feature_tags[{max(1, len(constraint_feature_tags))}] = {{")
    lines.extend(f"    {value}," for value in constraint_feature_tags)
    if not constraint_feature_tags: lines.append("    0,")
    lines.append("};")
    lines.append(f"static const u32 buster_a64_semantic_constraint_program_tokens[{max(1, len(constraint_program_tokens))}] = {{")
    lines.extend(f"    {value}," for value in constraint_program_tokens)
    if not constraint_program_tokens: lines.append("    0,")
    lines.append("};")
    lines.append(f"static const BusterA64SemanticGeneratedTransform buster_a64_semantic_transforms[{max(1, len(transforms))}] = {{")
    lines.extend(f"    {{{offsets[x['expression']]}, {x['source']}, {x['p0']}, {x['p1']}, {x['table_id']}, {x['program_first']}, {x['part_first']}, {x['value_first']}, {x['program_count']}, {x['part_count']}, {x['value_count']}, {x['kind']}, {1 if x['invertible'] else 0}, 0}}," for x in transforms)
    if not transforms: lines.append("    {0},")
    lines.append("};")
    lines.append(f"static const BusterA64SemanticGeneratedForm buster_a64_semantic_forms[{max(1, len(forms))}] = {{")
    lines.extend(f"    {{{offsets[x['name']]}, {offsets[x['mnemonic']]}, {offsets[x['assembly']]}, UINT64_C(0x{x['source_digest']:016x}), UINT32_C(0x{x['fixed_mask']:08x}), UINT32_C(0x{x['fixed_value']:08x}), {x['field_first']}, {x['operand_first']}, {x['transform_first']}, {x['field_count']}, {x['operand_count']}, {x['transform_count']}, {x['owner']}, {x['kind']}, {1 if x['raw_layout_resolved'] else 0}, {x['status']}}}," for x in forms)
    if not forms: lines.append("    {0},")
    lines.extend(["};", "#endif", ""])
    path.write_text("\n".join(lines))
    return len(pool), sha256(path)


def emit_jsonl(rows: list[dict[str, Any]], path: Path) -> tuple[int, str]:
    payload = "".join(json.dumps(row, ensure_ascii=False, separators=(",", ":"), sort_keys=True) + "\n" for row in rows)
    path.write_text(payload)
    return len(payload.encode()), sha256(path)


def emit_report(rows: list[dict[str, Any]], census: dict[str, Any], path: Path) -> tuple[int, str]:
    unsupported = [row for row in rows if row.get("binding_confidence") == "explicit-overlay" or any(x.get("kind") == "overlay_required" for x in row.get("transforms", []))]
    canonical = [row for row in rows if row["kind"] == "canonical"]
    aliases = [row for row in rows if row["kind"] == "alias"]
    undefined = [row for row in canonical if row.get("status") == "undefined"]
    report = {
        "schema_version": SCHEMA_VERSION,
        "canonical_sha256": CANONICAL_SHA256,
        "source_tree_sha256": SOURCE_TREE_SHA256,
        "rows": len(rows), "canonical_rows": len(canonical), "alias_rows": len(aliases),
        "status_composition": {"canonical_defined": sum(row.get("status") == "defined" for row in canonical), "canonical_undefined": len(undefined), "canonical_undefined_ids": [row["id"] for row in undefined], "canonical_denominator": len(canonical)},
        "owner_counts": census["owner_counts"], "canonical_owner_counts": census["canonical_owner_counts"],
        "memory_modes": census["memory_modes"], "cross_lenses": census["cross_lenses"],
        "semantic_kinds": census["semantic_kinds"], "transform_kinds": census["transform_kinds"],
        "table_key_classification": census.get("table_key_classification", {}),
        "string_pool_bytes": census.get("string_pool_bytes", 0), "largest_strings": census.get("largest_strings", []),
        "status": "binding_complete" if not unsupported else "binding_with_residuals",
        "binding_complete_rows": len(rows) - len(unsupported),
        "canonical_binding_complete_rows": len(canonical) - sum(row in unsupported for row in canonical),
        "alias_binding_complete_rows": len(aliases) - sum(row in unsupported for row in aliases),
        "executable_semantic_rows": 0,
        "canonical_executable_semantic_rows": 0,
        "alias_executable_semantic_rows": 0,
        "executable_semantics_status": "not-emitted",
        "parsed_vm_programs": census.get("parsed_vm_programs", 0),
        "typed_transform_programs": census.get("typed_transform_programs", 0),
        "typed_value_program_atoms": census.get("typed_value_program_atoms", 0),
        "typed_program_instructions": census.get("typed_program_instructions", 0),
        "typed_program_operands": census.get("typed_program_operands", 0),
        "residual_overlay_rows": census.get("residual_overlay_rows", 0),
        "numeric_relation_records": census.get("numeric_relation_records", 0),
        "numeric_relation_parsed": census.get("numeric_relation_parsed", 0),
        "numeric_relation_residual": census.get("numeric_relation_residual", 0),
        "numeric_relation_source_key_sha256": census.get("numeric_relation_source_key_sha256", ""),
        "operand_classification_status": "presentation-only",
        "unknown_role_operands": census.get("unknown_role_operands", 0),
        "ambiguous_role_operands": census.get("ambiguous_role_operands", 0),
        "explicit_overlay_rows": len(unsupported),
        "proven_overlay_rows": census["proven_overlay_rows"],
        "proven_overlay_ids": census["proven_overlay_ids"],
        "no_anchor_rows": census["no_anchor_rows"],
        "no_anchor_canonical_rows": census["no_anchor_canonical_rows"],
        "no_anchor_alias_rows": census["no_anchor_alias_rows"],
        "unsupported_or_gap_ids": [row["id"] for row in unsupported],
        "invariants": {"canonical_plus_alias": len(rows) == 1695, "owner_partition": sum(census["canonical_owner_counts"].values()) == 1523, "memory_partition": sum(census["memory_modes"].values()) == 559, "all_source_links_accounted": all(row.get("encoding_name") for row in rows), "all_canonical_binding_complete": not any(row["kind"] == "canonical" for row in unsupported), "no_row_claims_executable_semantics": not any(row.get("executable_semantics") for row in rows), "classification_is_not_authoritative": all(row.get("operand_classification_status") == "presentation-only" for row in rows), "undefined_udf_only": len(undefined) == 1 and undefined[0]["id"].endswith(":UDF_only_perm_undef"), "numeric_relation_source_projection_identity": census.get("numeric_relation_records", 0) == census.get("numeric_relation_parsed", 0) + census.get("numeric_relation_residual", 0), "table_key_classification_partition": (lambda values: values.get("key_cells", 0) == values.get("key_bitfield_cells", 0) + values.get("key_non_bitfield_cells", 0) + values.get("key_missing_class_cells", 0))(census.get("table_key_classification", {})), "residual_overlay_count_honest": census.get("residual_overlay_rows", 0) > 0 or census.get("numeric_relation_residual", 0) == 0},
    }
    path.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n")
    return len(path.read_bytes()), sha256(path)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--canonical", type=Path, required=True)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    actual_source_sha = source_tree_sha256(args.source)
    if actual_source_sha != SOURCE_TREE_SHA256:
        raise RuntimeError(f"source tree SHA-256 does not match the pinned input: {actual_source_sha}")
    rows = load_rows(args.canonical)
    semantic, census = semantic_rows(rows, args.source)
    validate_compact_projection(semantic)
    args.output.mkdir(parents=True, exist_ok=True)
    jsonl = args.output / "arm-a64-semantic.generated.jsonl"
    header = args.output / "arm-a64-semantic.generated.h"
    report = args.output / "arm-a64-semantic-report.json"
    manifest = args.output / "arm-a64-semantic-manifest.json"
    if args.check and manifest.exists():
        # Check mode is an integrity gate, not a repair command.  Validate
        # the bytes named by the existing manifest before regenerating so a
        # hand-edited JSONL/header/report cannot be silently overwritten.
        try:
            existing_manifest = json.loads(manifest.read_text())
            for artifact_name, artifact_path in (("jsonl", jsonl), ("header", header), ("report", report)):
                expected = existing_manifest.get("artifacts", {}).get(artifact_name, {}).get("sha256")
                actual = sha256(artifact_path) if artifact_path.exists() else None
                if expected != actual:
                    print(f"semantic artifact differs: {artifact_path}", file=sys.stderr)
                    return 1
        except (OSError, ValueError, TypeError):
            print(f"semantic manifest is invalid: {manifest}", file=sys.stderr)
            return 1
    json_bytes, json_digest = emit_jsonl(semantic, jsonl)
    pool_size, header_digest = generate_header(semantic, census, header)
    report_bytes, report_digest = emit_report(semantic, census, report)
    canonical = [row for row in semantic if row["kind"] == "canonical"]
    manifest_obj = {
        "schema_version": SCHEMA_VERSION, "status": "binding_complete" if not any(transform.get("kind") == "overlay_required" for row in semantic for transform in row.get("transforms", [])) else "binding_with_residuals", "executable_semantics_status": "not-emitted",
        "canonical": {"sha256": CANONICAL_SHA256, "rows": CANONICAL_COUNT, "canonical_rows": CANONICAL_DENOMINATOR, "alias_rows": ALIAS_DENOMINATOR, "status_composition": {"defined": sum(row.get("status") == "defined" for row in canonical), "undefined": sum(row.get("status") == "undefined" for row in canonical), "undefined_ids": [row["id"] for row in canonical if row.get("status") == "undefined"]}},
        "source": {"release": "2026-06", "tree_sha256": SOURCE_TREE_SHA256, "directory": str(args.source), "raw_source_policy": "raw Arm XML is not vendored; obtain the pinned release from Arm and verify this tree SHA-256 before regeneration"},
        "artifacts": {"jsonl": {"file": jsonl.name, "bytes": json_bytes, "sha256": json_digest}, "header": {"file": header.name, "bytes": header.stat().st_size, "sha256": header_digest, "string_pool_bytes": pool_size}, "report": {"file": report.name, "bytes": report_bytes, "sha256": report_digest}},
        "string_pool_bytes": census.get("string_pool_bytes", pool_size), "largest_strings": census.get("largest_strings", []),
        "owner_counts": census["owner_counts"], "canonical_owner_counts": census["canonical_owner_counts"], "memory_modes": census["memory_modes"], "cross_lenses": census["cross_lenses"], "transform_kinds": census["transform_kinds"], "table_key_classification": census.get("table_key_classification", {}), "parsed_vm_programs": census.get("parsed_vm_programs", 0), "typed_transform_programs": census.get("typed_transform_programs", 0), "typed_value_program_atoms": census.get("typed_value_program_atoms", 0), "typed_program_instructions": census.get("typed_program_instructions", 0), "typed_program_operands": census.get("typed_program_operands", 0), "residual_overlay_rows": census.get("residual_overlay_rows", 0), "numeric_relation_records": census.get("numeric_relation_records", 0), "numeric_relation_parsed": census.get("numeric_relation_parsed", 0), "numeric_relation_residual": census.get("numeric_relation_residual", 0), "numeric_relation_source_key_sha256": census.get("numeric_relation_source_key_sha256", ""), "operand_classification_status": "presentation-only", "unknown_role_operands": census.get("unknown_role_operands", 0), "ambiguous_role_operands": census.get("ambiguous_role_operands", 0),
        "coverage": {"binding_complete_rows": len(semantic) - census.get("residual_overlay_rows", 0), "canonical_binding_complete_rows": len(canonical) - sum(1 for row in canonical if any(transform.get("kind") == "overlay_required" for transform in row.get("transforms", []))), "alias_binding_complete_rows": len(semantic) - len(canonical), "executable_semantic_rows": 0, "canonical_executable_semantic_rows": 0, "alias_executable_semantic_rows": 0, "proven_overlay_rows": census["proven_overlay_rows"], "no_anchor_rows": census["no_anchor_rows"], "unsupported_or_gap_rows": census.get("residual_overlay_rows", 0)},
        "invariants": {"canonical_plus_alias": True, "owner_partition": sum(census["canonical_owner_counts"].values()) == CANONICAL_DENOMINATOR, "memory_partition": sum(census["memory_modes"].values()) == 559, "all_rows_accounted": len(semantic) == CANONICAL_COUNT, "all_canonical_binding_complete": not any(transform.get("kind") == "overlay_required" for row in canonical for transform in row.get("transforms", [])), "no_row_claims_executable_semantics": not any(row.get("executable_semantics") for row in semantic), "classification_is_not_authoritative": True, "numeric_relation_source_projection_identity": census.get("numeric_relation_records", 0) == census.get("numeric_relation_parsed", 0) + census.get("numeric_relation_residual", 0), "table_key_classification_partition": (lambda values: values.get("key_cells", 0) == values.get("key_bitfield_cells", 0) + values.get("key_non_bitfield_cells", 0) + values.get("key_missing_class_cells", 0))(census.get("table_key_classification", {})), "residual_overlay_count_honest": census.get("residual_overlay_rows", 0) > 0 or census.get("numeric_relation_residual", 0) == 0},
    }
    manifest_text = json.dumps(manifest_obj, indent=2, sort_keys=True) + "\n"
    if args.check and manifest.exists() and manifest.read_text() != manifest_text:
        print("semantic manifest differs", file=sys.stderr); return 1
    manifest.write_text(manifest_text)
    print(json.dumps({"rows": len(semantic), "owner_counts": census["owner_counts"], "memory_modes": census["memory_modes"], "jsonl_sha256": json_digest, "header_sha256": header_digest, "report_sha256": report_digest}, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
