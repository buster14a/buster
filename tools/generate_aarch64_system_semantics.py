#!/usr/bin/env python3
"""Generate the typed Apple-M1 A64 system semantic table.

The checked-in Arm canonical JSONL is the only source consumed here.  LLVM is
deliberately not consulted: it is an independent audit oracle, never a source
of production metadata.  The generator emits a pointer-free C header and can
check an existing output without changing it.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "src/buster/lib/compiler/assembly/generated/arm-a64-canonical.generated.jsonl"
DEFAULT_OUTPUT = ROOT / "src/buster/lib/compiler/assembly/generated/aarch64-system-semantics.generated.h"


ROWS_ORDER = [
    "BRK_EX_exception",
    "CLREX_BN_barriers",
    "DCPS1_DC_exception",
    "DCPS2_DC_exception",
    "DCPS3_DC_exception",
    "DMB_BO_barriers",
    "DSB_BO_barriers",
    "HINT_HM_hints",
    "HLT_EX_exception",
    "HVC_EX_exception",
    "ISB_BI_barriers",
    "MRS_RS_systemmove",
    "MSR_SI_pstate",
    "MSR_SR_systemmove",
    "SMC_EX_exception",
    "SVC_EX_exception",
    "SYSL_RC_systeminstrs",
    "SYS_CR_systeminstrs",
]

KIND = {
    "imm16": 0,
    "Rt": 1,
    "op1": 2,
    "CRn": 3,
    "CRm": 4,
    "o0": 5,
    "op2": 6,
}

FORM = {name: i for i, name in enumerate(ROWS_ORDER)}

# The checked-in Arm rows leave the option constraints implicit in their
# machine-readable field width.  These architectural masks are part of the
# pinned Apple-M1 closure and are emitted into the generated metadata so the
# encoder and both decode paths consume one deterministic constraint source.
# Bit N permits CRm=N.
VALUE_CONSTRAINTS = {
    "DMB_BO_barriers": (0, sum(1 << value for value in (1, 2, 3, 5, 6, 7, 9, 10, 11, 13, 14, 15))),
    "DSB_BO_barriers": (0, sum(1 << value for value in (0, 1, 2, 3, 4, 5, 6, 7, 9, 10, 11, 13, 14, 15))),
    "ISB_BI_barriers": (0, 1 << 15),
}


def load_rows() -> list[dict]:
    rows = []
    for line in SOURCE.read_text().splitlines():
        row = json.loads(line)
        if (
            row.get("kind") == "canonical"
            and row.get("system") is True
            and row.get("apple_m1") is True
            and int(row["field_mask"], 16) != 0
        ):
            rows.append(row)
    by_name = {row["encoding_name"]: row for row in rows}
    if sorted(by_name) != sorted(ROWS_ORDER):
        raise SystemExit(
            "system denominator drift: expected "
            + repr(ROWS_ORDER)
            + " got "
            + repr(sorted(by_name))
        )
    return [by_name[name] for name in ROWS_ORDER]


def c_escape(value: str) -> str:
    return (
        value.replace("\\", "\\\\")
        .replace('"', '\\"')
        .replace("\n", "\\n")
        .replace("\r", "\\r")
        .replace("\t", "\\t")
        .replace("\0", "\\0")
    )


def append_string(pool: bytearray, value: str) -> tuple[int, int]:
    encoded = value.encode("utf-8")
    offset = len(pool)
    pool.extend(encoded)
    pool.append(0)
    return offset, len(encoded)


def generate() -> str:
    rows = load_rows()
    pool = bytearray()
    string_refs: dict[str, tuple[int, int]] = {}

    def string_ref(value: str) -> tuple[int, int]:
        if value not in string_refs:
            string_refs[value] = append_string(pool, value)
        return string_refs[value]

    fields: list[dict] = []
    row_meta: list[dict] = []
    for row in rows:
        first = len(fields)
        for field in row["fields"]:
            name = field["name"]
            if name not in KIND:
                raise SystemExit(f"unmapped Arm field {name} in {row['encoding_name']}")
            segments = field["segments"]
            if len(segments) != 1:
                raise SystemExit(f"unexpected split system field {name} in {row['encoding_name']}")
            segment = segments[0]
            fields.append(
                {
                    "name": string_ref(name),
                    "kind": KIND[name],
                    "width": segment["width"],
                    "instruction_lsb": segment["instruction_lsb"],
                    "value_lsb": segment["value_lsb"],
                }
            )
        optional_mask = 0
        default_value = 0
        if row["encoding_name"] in {"CLREX_BN_barriers", "ISB_BI_barriers"}:
            optional_mask = 1
            default_value = 15
        elif row["encoding_name"] == "SYS_CR_systeminstrs":
            optional_mask = 1
            default_value = 31
        flags = 0
        if row["encoding_name"] == "HINT_HM_hints":
            flags = 1
        elif row["encoding_name"] == "MSR_SI_pstate":
            flags = 2
        elif row["encoding_name"] in {"MRS_RS_systemmove", "MSR_SR_systemmove"}:
            flags = 4
        constraint_field, constraint_mask = VALUE_CONSTRAINTS.get(row["encoding_name"], (0xFF, 0))
        row_meta.append(
            {
                "id": string_ref(row["id"]),
                "encoding_name": string_ref(row["encoding_name"]),
                "mnemonic": string_ref(row["assembly"].split(" ", 1)[0]),
                "assembly": string_ref(row["assembly"]),
                "digest": int(row["digest"], 16),
                "fixed_mask": int(row["fixed_mask"], 16),
                "fixed_value": int(row["fixed_value"], 16),
                "field_mask": int(row["field_mask"], 16),
                "field_first": first,
                "field_count": len(row["fields"]),
                "optional_mask": optional_mask,
                "default_value": default_value,
                "flags": flags,
                "constraint_field": constraint_field,
                "constraint_mask": constraint_mask,
            }
        )

    digest_text = "".join(f"{row['id']}\t{row['digest']}\n" for row in sorted(rows, key=lambda r: r["id"]))
    digest = hashlib.sha256(digest_text.encode("ascii")).hexdigest()

    out: list[str] = []
    out.extend(
        [
            "/* Generated by tools/generate_aarch64_system_semantics.py; do not edit. */",
            "#ifndef BUSTER_AARCH64_SYSTEM_SEMANTICS_GENERATED_H",
            "#define BUSTER_AARCH64_SYSTEM_SEMANTICS_GENERATED_H",
            "#include <buster/lib/base.h>",
            "",
            "#define BUSTER_AARCH64_SYSTEM_SEMANTICS_GENERATED_SCHEMA_VERSION 2u",
            f"#define BUSTER_AARCH64_SYSTEM_SEMANTICS_GENERATED_ROW_COUNT {len(rows)}u",
            f"#define BUSTER_AARCH64_SYSTEM_SEMANTICS_GENERATED_FIELD_COUNT {len(fields)}u",
            f'#define BUSTER_AARCH64_SYSTEM_SEMANTICS_GENERATED_DIGEST "{digest}"',
            "",
            "typedef struct BusterAarch64SystemGeneratedString BusterAarch64SystemGeneratedString;",
            "struct BusterAarch64SystemGeneratedString { u32 offset; u32 length; };",
            "typedef struct BusterAarch64SystemGeneratedField BusterAarch64SystemGeneratedField;",
            "struct BusterAarch64SystemGeneratedField {",
            "    BusterAarch64SystemGeneratedString name;",
            "    u8 kind; u8 width; u8 instruction_lsb; u8 value_lsb;",
            "};",
            "typedef struct BusterAarch64SystemGeneratedRow BusterAarch64SystemGeneratedRow;",
            "struct BusterAarch64SystemGeneratedRow {",
            "    BusterAarch64SystemGeneratedString id;",
            "    BusterAarch64SystemGeneratedString encoding_name;",
            "    BusterAarch64SystemGeneratedString mnemonic;",
            "    BusterAarch64SystemGeneratedString assembly;",
            "    u64 row_digest;",
            "    u32 fixed_mask; u32 fixed_value; u32 field_mask;",
            "    u16 field_first; u8 field_count; u8 optional_mask;",
            "    u8 default_value; u8 flags; u8 constraint_field; u8 reserved;",
            "    u16 constraint_mask;",
            "};",
            "",
            "static const char8 buster_aarch64_system_generated_string_pool[] =",
        ]
    )
    # Keep a trailing NUL so range validation can check its sentinel.
    pool_text = "".join(chr(c) for c in pool)
    out.append('    "' + c_escape(pool_text) + '";')
    out.append(f"#define BUSTER_AARCH64_SYSTEM_SEMANTICS_GENERATED_STRING_POOL_SIZE {len(pool)}u")
    out.append("")
    out.append("static const BusterAarch64SystemGeneratedField buster_aarch64_system_generated_fields[] = {")
    for field in fields:
        out.append(
            "    {{ {{UINT32_C({}), UINT32_C({})}}, {}, {}, {}, {} }},".format(
                field["name"][0], field["name"][1], field["kind"], field["width"], field["instruction_lsb"], field["value_lsb"]
            )
        )
    out.append("};")
    out.append("")
    out.append("static const BusterAarch64SystemGeneratedRow buster_aarch64_system_generated_rows[] = {")
    for row in row_meta:
        out.append(
            "    {{ {{UINT32_C({}), UINT32_C({})}}, {{UINT32_C({}), UINT32_C({})}}, {{UINT32_C({}), UINT32_C({})}}, "
            "{{UINT32_C({}), UINT32_C({})}}, UINT64_C(0x{:016x}), UINT32_C(0x{:08x}), UINT32_C(0x{:08x}), "
            "UINT32_C(0x{:08x}), {}, {}, {}, {}, {}, {}, 0, UINT16_C(0x{:04x}) }},".format(
                row["id"][0],
                row["id"][1],
                row["encoding_name"][0],
                row["encoding_name"][1],
                row["mnemonic"][0],
                row["mnemonic"][1],
                row["assembly"][0],
                row["assembly"][1],
                row["digest"],
                row["fixed_mask"],
                row["fixed_value"],
                row["field_mask"],
                row["field_first"],
                row["field_count"],
                row["optional_mask"],
                row["default_value"],
                row["flags"],
                row["constraint_field"],
                row["constraint_mask"],
            )
        )
    out.extend(
        [
            "};",
            "",
            "#endif",
            "",
        ]
    )
    return "\n".join(out)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument("--check", action="store_true", help="fail if output is not exactly regenerated")
    args = parser.parse_args()
    expected = generate()
    if args.check:
        try:
            actual = args.output.read_text()
        except OSError:
            return 1
        return 0 if actual == expected else 1
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(expected)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
