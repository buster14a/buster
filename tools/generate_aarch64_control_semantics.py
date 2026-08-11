#!/usr/bin/env python3
"""Derive and verify the pinned Apple-M1 A64 control semantic slice.

Normal builds consume the checked-in generated artifacts directly.  This
developer tool is deliberately deterministic: it reads the pinned canonical
JSONL, selects exactly the 27 approved rows, derives the compact semantic
projection, and verifies the cross-lens digest partitions.  ``--check`` does
not write anything and fails on membership, digest, or artifact drift.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_SOURCE = ROOT / "src/buster/lib/compiler/assembly/generated/arm-a64-canonical.generated.jsonl"
DEFAULT_JSONL = ROOT / "src/buster/lib/compiler/assembly/generated/aarch64-control-semantics.generated.jsonl"
DEFAULT_MANIFEST = ROOT / "src/buster/lib/compiler/assembly/generated/aarch64-control-semantics-manifest.json"
DEFAULT_HEADER = ROOT / "src/buster/lib/compiler/assembly/generated/aarch64-control-semantics.generated.h"

ROW_DIGESTS = {
    "ADRP_only_pcreladdr": "0xd7d70d13c8dc068d",
    "ADR_only_pcreladdr": "0x25523e69dae2dcd2",
    "BL_only_branch_imm": "0x2e065d453ddb5bb4",
    "B_only_branch_imm": "0x1c3f5d16e7f47c7b",
    "B_only_condbranch": "0x493dad8e510dddab",
    "CBNZ_32_compbranch": "0x929f58d3f6754da2",
    "CBNZ_64_compbranch": "0x3f3ae4ea929693c8",
    "CBZ_32_compbranch": "0x509f5dc8050a5eb",
    "CBZ_64_compbranch": "0xd01729aadc71e687",
    "CSEL_32_condsel": "0xbb0b933d1b2bf78e",
    "CSEL_64_condsel": "0x92b646113cee0d7e",
    "CSINC_32_condsel": "0x2b052232cd75d008",
    "CSINC_64_condsel": "0xb984d9b8eb17ebbf",
    "CSINV_32_condsel": "0xd5f34dfe86160a7a",
    "CSINV_64_condsel": "0x2674f1de193b7d25",
    "CSNEG_32_condsel": "0x77eb53be7e4cedc0",
    "CSNEG_64_condsel": "0xffa16a3978da4452",
    "LDRSW_64_loadlit": "0xca197147f8b7c99f",
    "LDR_32_loadlit": "0x19a0849fc1368185",
    "LDR_64_loadlit": "0x6445455f3888f467",
    "LDR_D_loadlit": "0x29a16412f15fef9e",
    "LDR_Q_loadlit": "0xed4683e2f659a7f8",
    "LDR_S_loadlit": "0x15b43bbc17d930ee",
    "PRFM_P_loadlit": "0xe8617a435eefc338",
    "RET_64R_branch_reg": "0x27aab29ac0c28070",
    "TBNZ_only_testbranch": "0xde51e9636e9f1654",
    "TBZ_only_testbranch": "0xdb8b9b82be4eb846",
}

FORMS = {
    "ADRP_only_pcreladdr": "ADRP",
    "ADR_only_pcreladdr": "ADR",
    "BL_only_branch_imm": "BL",
    "B_only_branch_imm": "B",
    "B_only_condbranch": "B_COND",
    "CBNZ_32_compbranch": "CBNZ_W",
    "CBNZ_64_compbranch": "CBNZ_X",
    "CBZ_32_compbranch": "CBZ_W",
    "CBZ_64_compbranch": "CBZ_X",
    "CSEL_32_condsel": "CSEL_W",
    "CSEL_64_condsel": "CSEL_X",
    "CSINC_32_condsel": "CSINC_W",
    "CSINC_64_condsel": "CSINC_X",
    "CSINV_32_condsel": "CSINV_W",
    "CSINV_64_condsel": "CSINV_X",
    "CSNEG_32_condsel": "CSNEG_W",
    "CSNEG_64_condsel": "CSNEG_X",
    "LDRSW_64_loadlit": "LDRSW_X",
    "LDR_32_loadlit": "LDR_W",
    "LDR_64_loadlit": "LDR_X",
    "LDR_D_loadlit": "LDR_D",
    "LDR_Q_loadlit": "LDR_Q",
    "LDR_S_loadlit": "LDR_S",
    "PRFM_P_loadlit": "PRFM",
    "RET_64R_branch_reg": "RET",
    "TBNZ_only_testbranch": "TBNZ",
    "TBZ_only_testbranch": "TBZ",
}

ORACLE_WORDS = {
    "CSEL_32_condsel": 0x1A820020,
    "CSEL_64_condsel": 0x9A820020,
    "CSINC_32_condsel": 0x1A820420,
    "CSINC_64_condsel": 0x9A820420,
    "CSINV_32_condsel": 0x5A820020,
    "CSINV_64_condsel": 0xDA820020,
    "CSNEG_32_condsel": 0x5A820420,
    "CSNEG_64_condsel": 0xDA820420,
    "RET_64R_branch_reg": 0xD65F03C0,
}

GROUPS = {
    "general_label_fixup": {name for name in ROW_DIGESTS if name not in FORMS or FORMS[name] not in {
        "CSEL_W", "CSEL_X", "CSINC_W", "CSINC_X", "CSINV_W", "CSINV_X", "CSNEG_W", "CSNEG_X", "RET"
    } and name not in {"LDR_S_loadlit", "LDR_D_loadlit", "LDR_Q_loadlit"}},
    "general_condition_select": {name for name, form in FORMS.items() if form in {
        "CSEL_W", "CSEL_X", "CSINC_W", "CSINC_X", "CSINV_W", "CSINV_X", "CSNEG_W", "CSNEG_X"
    }},
    "general_ret": {"RET_64R_branch_reg"},
    "complex_literal_load": {"LDR_S_loadlit", "LDR_D_loadlit", "LDR_Q_loadlit"},
}

EXPECTED_DIGESTS = {
    "general_label_fixup": "fff799a7ccd6dbe7adb9bc238b4b5838b6dd02928450e2048a579c645e891a15",
    "general_condition_select": "a6aa6bcea3eaada795da6acbaac131afe8d8eeae58839cf5cb509934dafcb0ad",
    "general_ret": "babf7e807a273b459d0fb4caa94e41b9779c9949ec2f7bf4eda1803aa7a734d6",
    "complex_literal_load": "2a29a4d0e6b7d537dde6523e1859678169a4c93c973e4bd1d4f6e87fa6d6e25e",
}
EXPECTED_ALL_DIGEST = "981200c3a2350be0f18eb8785929b30de5f468b5a8faa6419d96b7fa3b5e42a4"


def digest(rows: list[dict[str, Any]]) -> str:
    payload = "".join(f"{row['id']}\t{row['digest']}\n" for row in sorted(rows, key=lambda row: row["id"]))
    return hashlib.sha256(payload.encode("utf-8")).hexdigest()


def reg(role: str, width: int, register_class: str = "GPR", register31: str = "ZR") -> list[Any]:
    return [role, "register", width, 0, 31, register31, register_class]


def pc() -> list[Any]:
    return ["target", "pc_relative", 64]


def operands(form: str) -> list[list[Any]]:
    if form in {"ADRP", "ADR"}:
        return [reg("destination", 64), pc()]
    if form in {"B", "BL"}:
        return [pc()]
    if form == "B_COND":
        return [pc(), ["condition", "condition", 4, 0, 15]]
    if form in {"CBZ_W", "CBNZ_W"}:
        return [reg("test", 32), pc()]
    if form in {"CBZ_X", "CBNZ_X"}:
        return [reg("test", 64), pc()]
    if form in {"TBZ", "TBNZ"}:
        return [reg("test", 0), ["bit", "immediate", 6, 0, 63], pc()]
    if form in {"CSEL_W", "CSINC_W", "CSINV_W", "CSNEG_W"}:
        return [reg("destination", 32), reg("source_n", 32), reg("source_m", 32), ["condition", "condition", 4, 0, 15]]
    if form in {"CSEL_X", "CSINC_X", "CSINV_X", "CSNEG_X"}:
        return [reg("destination", 64), reg("source_n", 64), reg("source_m", 64), ["condition", "condition", 4, 0, 15]]
    if form == "LDRSW_X":
        return [reg("destination", 64), pc()]
    if form == "LDR_W":
        return [reg("destination", 32), pc()]
    if form == "LDR_X":
        return [reg("destination", 64), pc()]
    if form == "LDR_S":
        return [reg("destination", 32, "FP_SIMD", "NONE"), pc()]
    if form == "LDR_D":
        return [reg("destination", 64, "FP_SIMD", "NONE"), pc()]
    if form == "LDR_Q":
        return [reg("destination", 128, "FP_SIMD", "NONE"), pc()]
    if form == "PRFM":
        return [["prefetch", "immediate", 5, 0, 31], pc()]
    if form == "RET":
        return [reg("target", 64)]
    raise ValueError(f"unhandled form {form}")


def pc_relative(form: str) -> list[Any] | None:
    if form in {"ADRP"}:
        return ["ADRP", 21, 12, 4096, -4294967296, 4294963200]
    if form in {"ADR"}:
        return ["ADR", 21, 0, 1, -1048576, 1048575]
    if form in {"B", "BL"}:
        return ["IMM26", 26, 2, 4, -134217728, 134217724]
    if form == "B_COND" or form.startswith("CBZ") or form.startswith("CBNZ") or form in {
        "LDRSW_X", "LDR_W", "LDR_X", "LDR_S", "LDR_D", "LDR_Q", "PRFM"
    }:
        return ["IMM19", 19, 2, 4, -1048576, 1048572]
    if form in {"TBZ", "TBNZ"}:
        return ["IMM14", 14, 2, 4, -32768, 32764]
    return None


def fixup(form: str) -> str:
    return {
        "ADRP": "ADRP_PAGE21", "ADR": "ADR_BYTE21", "B": "BRANCH26", "BL": "CALL26",
        "B_COND": "B_COND19", "TBZ": "TEST14", "TBNZ": "TEST14",
        "CBZ_W": "COMPARE19", "CBZ_X": "COMPARE19", "CBNZ_W": "COMPARE19", "CBNZ_X": "COMPARE19",
        "LDRSW_X": "LITERAL19", "LDR_W": "LITERAL19", "LDR_X": "LITERAL19", "LDR_S": "LITERAL19",
        "LDR_D": "LITERAL19", "LDR_Q": "LITERAL19", "PRFM": "LITERAL19",
    }.get(form, "NONE")


def relocation(form: str) -> str:
    return "darwin_external_branch26" if form in {"B", "BL"} else ("local_only" if pc_relative(form) else "none")


def project(canonical: dict[str, Any]) -> dict[str, Any]:
    name = canonical["id"].split(":", 1)[1]
    form = FORMS[name]
    row: dict[str, Any] = {
        "schema_version": 1,
        "id": canonical["id"],
        "digest": canonical["digest"],
        "owner": "complex_literal" if form in {"LDR_S", "LDR_D", "LDR_Q"} else "general",
        "form": form,
        "assembly": canonical["assembly"],
        "fixed_mask": canonical["fixed_mask"],
        "fixed_value": canonical["fixed_value"],
        "oracle_word": f"0x{ORACLE_WORDS.get(name, int(canonical['fixed_value'], 16)):08x}",
        "operands": operands(form),
    }
    if form == "RET": row["default_operand"] = 30
    if pc_relative(form) is not None: row["pc_relative"] = pc_relative(form)
    row["fixup"] = fixup(form)
    row["relocation"] = relocation(form)
    return row


def load_rows(source: Path) -> list[dict[str, Any]]:
    found: dict[str, dict[str, Any]] = {}
    with source.open(encoding="utf-8") as stream:
        for line in stream:
            row = json.loads(line)
            name = row.get("id", "").split(":", 1)[-1]
            if name in ROW_DIGESTS:
                if name in found: raise ValueError(f"duplicate selected canonical row: {name}")
                found[name] = row
    missing = sorted(set(ROW_DIGESTS) - set(found))
    if missing: raise ValueError(f"missing selected canonical rows: {', '.join(missing)}")
    for name, row in found.items():
        if row.get("digest") != ROW_DIGESTS[name] or not row.get("apple_m1") or row.get("kind") != "canonical":
            raise ValueError(f"canonical drift for {name}: digest/membership/kind/apple_m1 mismatch")
        if not (3 <= len(row["digest"]) <= 18) or not row["digest"].startswith("0x") or any(character not in "0123456789abcdef" for character in row["digest"][2:]):
            raise ValueError(f"invalid canonical row digest spelling: {name}")
    return [project(found[name]) for name in sorted(ROW_DIGESTS)]


def artifact(path: Path) -> dict[str, Any]:
    data = path.read_bytes()
    return {"file": path.name, "bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()}


def render_jsonl(rows: list[dict[str, Any]]) -> bytes:
    return b"".join((json.dumps(row, separators=(",", ":"), ensure_ascii=True) + "\n").encode("utf-8") for row in rows)


def render_manifest(rows: list[dict[str, Any]], source: Path, jsonl: Path, header: Path) -> bytes:
    groups: dict[str, dict[str, Any]] = {}
    by_name = {row["id"].split(":", 1)[1]: row for row in rows}
    for name, members in GROUPS.items():
        selected = [by_name[member] for member in sorted(members)]
        groups[name] = {"count": len(selected), "digest": digest(selected)}
    manifest = {
        "schema_version": 2,
        "source": source.name,
        "profile": "apple-m1",
        "row_count": len(rows),
        "digest_recipe": "sorted id<TAB>row.digest<NL>",
        "digest": digest(rows),
        "groups": groups,
        "artifacts": {
            "source": artifact(source),
            "jsonl": artifact(jsonl),
            "header": artifact(header),
        },
    }
    return (json.dumps(manifest, indent=2, ensure_ascii=True) + "\n").encode("utf-8")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true", help="verify checked-in artifacts without writing")
    parser.add_argument("--source", type=Path, default=DEFAULT_SOURCE)
    parser.add_argument("--jsonl", type=Path, default=DEFAULT_JSONL)
    parser.add_argument("--manifest", type=Path, default=DEFAULT_MANIFEST)
    parser.add_argument("--header", type=Path, default=DEFAULT_HEADER)
    args = parser.parse_args()
    try:
        if any(len(value) != 64 or any(character not in "0123456789abcdef" for character in value) for value in [EXPECTED_ALL_DIGEST, *EXPECTED_DIGESTS.values()]):
            raise ValueError("expected digest constants must be lowercase 64-character SHA-256 strings")
        rows = load_rows(args.source)
        if digest(rows) != EXPECTED_ALL_DIGEST:
            raise ValueError("selected row digest drift")
        for group, expected in EXPECTED_DIGESTS.items():
            members = [row for row in rows if row["id"].split(":", 1)[1] in GROUPS[group]]
            if digest(members) != expected:
                raise ValueError(f"group digest drift: {group}")
        expected_jsonl = render_jsonl(rows)
        if args.check:
            if args.jsonl.read_bytes() != expected_jsonl:
                raise ValueError(f"generated JSONL drift: {args.jsonl}")
            expected_manifest = render_manifest(rows, args.source, args.jsonl, args.header)
            if args.manifest.read_bytes() != expected_manifest:
                raise ValueError(f"generated manifest drift: {args.manifest}")
        else:
            args.jsonl.write_bytes(expected_jsonl)
            args.manifest.write_bytes(render_manifest(rows, args.source, args.jsonl, args.header))
    except (OSError, ValueError, json.JSONDecodeError) as error:
        parser.error(str(error))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
