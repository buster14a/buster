#!/usr/bin/env python3
"""Reproduce hypothetical module-entry alignment accounting; never rewrite ELF.

Run beside layout_census.py, using the hosted evidence directory containing
unity.json, fixtures.json and their .o inputs. Fail if extents are missing,
overlap, fail align16, contain unfamiliar gap bytes, or leave trailing data.
"""

import argparse
import hashlib
import json
from pathlib import Path

from layout_census import read_elf


METRICS = ("baseline_bytes", "one_shot_bytes", "shortest_widen_bytes")


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def account_object(evidence, census_object, functions):
    path = evidence / Path(census_object["path"]).name
    data, sections, symbols, unused_relocations = read_elf(path)
    object_hash = hashlib.sha256(data).hexdigest()
    if object_hash != census_object["sha256"]:
        raise ValueError(f"{path.name}: object identity disagrees with source census")
    text_sections = [section for section in sections if section["name"] == ".text"]
    if len(text_sections) != 1 or text_sections[0]["alignment"] != 16:
        raise ValueError(f"{path.name}: unfamiliar .text section count/alignment")
    text = text_sections[0]
    bodies = sorted((f for f in functions if f["object"] == census_object["path"] and f["section"] == ".text"),
                    key=lambda function: function["original_start"])
    expected_extents = {(s["value"], s["size"]) for s in symbols
                        if s["type"] == 2 and s["section"] == text["index"] and s["size"]}
    observed_extents = {(f["original_start"], f["baseline_bytes"]) for f in bodies}
    if expected_extents != observed_extents or len(observed_extents) != len(bodies):
        raise ValueError(f"{path.name}: census does not cover every unique positive-sized function extent")
    cursor, observed_padding = 0, 0
    for function in bodies:
        start = function["original_start"]
        expected_start = (cursor + 15) & ~15
        if start != expected_start:
            raise ValueError(f"{path.name}: {function['name']} is not the next align16 extent")
        gap = text["data"][cursor:start]
        if any(byte != 0x90 for byte in gap):
            raise ValueError(f"{path.name}: opaque/non-NOP gap before {function['name']}; preserve it, do not discard it")
        observed_padding += len(gap)
        cursor = start + function["baseline_bytes"]
    if cursor != text["size"]:
        raise ValueError(f"{path.name}: unfamiliar trailing/undelimited .text bytes")
    models = {}
    for metric in METRICS:
        cursor, padding = 0, 0
        for function in bodies:
            aligned = (cursor + 15) & ~15
            padding += aligned - cursor
            cursor = aligned + function[metric]
        models[metric] = {
            "function_body_bytes": sum(function[metric] for function in bodies),
            "entry_alignment_padding_bytes": padding,
            "text_bytes": cursor,
            "saved_text_bytes": text["size"] - cursor,
        }
    if models["baseline_bytes"]["text_bytes"] != text["size"] or models["baseline_bytes"]["entry_alignment_padding_bytes"] != observed_padding:
        raise ValueError(f"{path.name}: baseline accounting disagreement")
    candidates = [function for function in bodies if function["candidate_count"]]
    distribution = {
        "functions": len(bodies),
        "zero_candidates": sum(function["candidate_count"] == 0 for function in bodies),
        "one_through_twelve_candidates": sum(1 <= function["candidate_count"] <= 12 for function in bodies),
        "more_than_twelve_candidates": sum(function["candidate_count"] > 12 for function in bodies),
        "max_candidates": max((function["candidate_count"] for function in bodies), default=0),
        "candidate_containing_functions": len(candidates),
        "candidate_functions_with_code_relocations": sum(function["code_relocations_requiring_offset_mapping"] > 0 for function in candidates),
        "candidate_functions_with_rip_relative_instructions": sum(function["rip_relative_instructions_requiring_separate_fixup_authority"] > 0 for function in candidates),
        "candidate_functions_with_extra_symbol_boundaries": sum(function["symbols_requiring_offset_mapping"] > 1 for function in candidates),
        "candidate_functions_with_fixed_rel8_edges": sum(function["fixed_rel8_edges"] > 0 for function in candidates),
        "candidate_edges_in_oracle_functions": sum(function["candidate_count"] for function in bodies if function["candidate_count"] <= 12),
        "candidate_edges_beyond_oracle_limit": sum(function["candidate_count"] for function in bodies if function["candidate_count"] > 12),
        "functions_improved_widening_over_one_shot": sum(function["shortest_widen_bytes"] < function["one_shot_bytes"] for function in bodies),
    }
    return {
        "object": path.name,
        "object_sha256": object_hash,
        "observed_text_bytes": text["size"],
        "observed_entry_padding_bytes": observed_padding,
        "baseline_extents_and_gap_bytes_checked": True,
        "opaque_interfunction_or_trailing_bytes": 0,
        "hypothetical_realigned_sizes": models,
        "distribution": distribution,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("evidence_directory", type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    groups, revisions = [], set()
    for filename in ("unity.json", "fixtures.json"):
        path = args.evidence_directory / filename
        census = json.loads(path.read_text())
        revisions.add(census["revision"])
        objects = [account_object(args.evidence_directory, obj, census["functions"]) for obj in census["objects"]]
        aggregate = {metric: {key: sum(obj["hypothetical_realigned_sizes"][metric][key] for obj in objects)
                              for key in ("function_body_bytes", "entry_alignment_padding_bytes", "text_bytes", "saved_text_bytes")}
                     for metric in METRICS}
        groups.append({"census": filename, "source_census_sha256": sha256(path),
                       "source_checked_no_inline_asm": census["source_checked_no_inline_asm"],
                       "objects": objects, "aggregate_text_accounting": aggregate})
    if len(revisions) != 1:
        raise ValueError("input census revisions disagree")
    output = {
        "revision": next(iter(revisions)),
        "evidence_class": "offline accounting over hosted Buster object/census evidence; no rewrite",
        "layout_recurrence": "cursor=0; each ordered function: start=(cursor+15)&~15; cursor=start+modeled_bytes; no trailing alignment",
        "assumptions": ["Preserve function order and freeze all modeled intrafunction fragments/padding",
                        "Reapply only verified source-owned 16-byte module entry alignment"],
        "limits": ["No per-function absence-of-inline-assembly-alignment proof for unity",
                   "All references, relocation places/addends, symbol values and debug/unwind metadata need their original target-specific authorities",
                   "No rewritten-object legality, eligible production savings or throughput claim"],
        "inputs": groups,
    }
    args.output.write_text(json.dumps(output, indent=2, sort_keys=True) + "\n")
    print(json.dumps({group["census"]: group["aggregate_text_accounting"] for group in groups}, sort_keys=True))


if __name__ == "__main__":
    main()
