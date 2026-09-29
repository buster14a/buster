#!/usr/bin/env python3
"""Offline event census; exact traces and logical models, never timing evidence.

Input: artifact directory containing one or more sequential *.trace streams.
Output: census.json in that directory, plus a concise trace/bucket table.
Ownership: standalone research tooling, no compiler or canonical-IR changes.
"""

import argparse
from collections import Counter
import json
from pathlib import Path
import sys


U16_MAX = 65535
U32_MAX = 4294967295
VALID = 1
CHECKED = 2
FLAG_MASK = 15
CACHE_LINE = 64
REASONS = ("empty", "mode", "end", "scope", "type")
BUCKETS = ("0_16", "17_64", "65_256", "257_1024", "over_1024")


def flags_match(stored, requested):
    return (stored & ~CHECKED) == (requested & ~CHECKED) and (stored & requested) == requested


def population_bucket(tokens):
    if tokens <= 16:
        result = BUCKETS[0]
    elif tokens <= 64:
        result = BUCKETS[1]
    elif tokens <= 256:
        result = BUCKETS[2]
    elif tokens <= 1024:
        result = BUCKETS[3]
    else:
        result = BUCKETS[4]
    return result


def field_lines(offset, width):
    return range(offset // CACHE_LINE, (offset + width - 1) // CACHE_LINE + 1)


def new_body(path, ordinal, tokens, start, scope_count, type_count):
    return {
        "trace": path.name,
        "body_index": ordinal,
        "token_count": tokens,
        "body_start": start,
        "scope_count_at_begin": scope_count,
        "type_count_at_begin": type_count,
        "bucket": population_bucket(tokens),
        "narrow_feasible": tokens <= U16_MAX,
        "queries": 0,
        "root_queries": 0,
        "nested_queries": 0,
        "hits": 0,
        "writes": 0,
        "overwrites": 0,
        "mismatches": 0,
        "miss_reasons_flags_first": Counter(),
        "miss_reasons_current_order": Counter(),
        "miss_conditions_overlap": Counter(),
        "scope_gt_u16_publications": 0,
        "type_gt_u16_publications": 0,
        "scope_gt_u16_queries": 0,
        "type_count_gt_u16_queries": 0,
        "scope_count_gt_u16_at_begin": int(scope_count > U16_MAX),
        "type_count_gt_u16_at_begin": int(type_count > U16_MAX),
        "max_scope_id": 0,
        "max_type_id": 0,
        "max_query_type_count": 0,
        "current_init_bytes": tokens * 16,
        "hot_cold_init_bytes": tokens,
        "current_storage_bytes": tokens * 16,
        "hot_cold_storage_bytes": tokens * 13,
        "narrow_storage_bytes": tokens * (12 if tokens <= U16_MAX else 16),
        "current_query_payload_probes": 0,
        "hot_cold_query_payload_probes": 0,
        "_memo": {},
        "_query_slots": set(),
        "_current_query_lines": set(),
        "_hot_cold_query_lines": set(),
        "_current_write_lines": set(),
        "_hot_cold_write_lines": set(),
    }


def lookup(body, fields, location):
    reader, slot, end, scope, flags, type_count, expected = fields
    if reader > 1 or expected > 1:
        raise ValueError(f"{location}: invalid reader or expected_hit")
    validate_slot(body, slot, end, flags, location)
    stored_end, stored_scope, stored_type, stored_flags = body["_memo"].get(slot, (0, 0, 0, 0))
    matched_flags = flags_match(stored_flags, flags)
    matched_end = stored_end == end
    matched_scope = stored_scope == scope
    matched_type = stored_type < type_count
    hit = matched_end and matched_scope and matched_flags and matched_type
    body["queries"] += 1
    body["root_queries" if reader else "nested_queries"] += 1
    body["hits"] += int(hit)
    body["mismatches"] += int(hit != bool(expected))
    body["_query_slots"].add(slot)
    body["scope_gt_u16_queries"] += int(scope > U16_MAX)
    body["type_count_gt_u16_queries"] += int(type_count > U16_MAX)
    body["max_scope_id"] = max(body["max_scope_id"], scope)
    body["max_query_type_count"] = max(body["max_query_type_count"], type_count)

    # Current code always probes end, then scope, then flags, then type.
    current_fields = [0]
    if matched_end:
        current_fields.append(1)
        if matched_scope and matched_flags:
            current_fields.append(2)
    body["current_query_payload_probes"] += 1
    for field in current_fields:
        body["_current_query_lines"].update(field_lines(slot * 16 + field * 4, 4))

    # Hot/cold candidates gate all payload reads on the matching flags.
    if matched_flags:
        body["hot_cold_query_payload_probes"] += 1
        hot_fields = [0]
        if matched_end:
            hot_fields.append(1)
            if matched_scope:
                hot_fields.append(2)
        for field in hot_fields:
            body["_hot_cold_query_lines"].update(field_lines(slot * 12 + field * 4, 4))

    if not hit:
        conditions = {
            "empty": not (stored_flags & VALID),
            "mode": not matched_flags,
            "end": not matched_end,
            "scope": not matched_scope,
            "type": not matched_type,
        }
        for reason in REASONS:
            if conditions[reason]:
                body["miss_conditions_overlap"][reason] += 1
        first = next(reason for reason in REASONS if conditions[reason])
        body["miss_reasons_flags_first"][first] += 1
        if not matched_end:
            current_reason = "end"
        elif not matched_scope:
            current_reason = "scope"
        elif not matched_flags:
            current_reason = "empty" if not (stored_flags & VALID) else "mode"
        else:
            current_reason = "type"
        body["miss_reasons_current_order"][current_reason] += 1


def validate_slot(body, slot, end, flags, location):
    if slot >= body["token_count"] or not (body["body_start"] + slot < end <= body["body_start"] + body["token_count"]):
        raise ValueError(f"{location}: slot/end outside the live body")
    if not (flags & VALID) or flags & ~FLAG_MASK:
        raise ValueError(f"{location}: invalid memo flags")


def publish(body, fields, location):
    slot, end, scope, type_id, flags = fields
    validate_slot(body, slot, end, flags, location)
    body["writes"] += 1
    body["overwrites"] += int(slot in body["_memo"])
    body["scope_gt_u16_publications"] += int(scope > U16_MAX)
    body["type_gt_u16_publications"] += int(type_id > U16_MAX)
    body["max_scope_id"] = max(body["max_scope_id"], scope)
    body["max_type_id"] = max(body["max_type_id"], type_id)
    body["_memo"][slot] = (end, scope, type_id, flags)
    body["_current_write_lines"].update(field_lines(slot * 16, 12))
    body["_hot_cold_write_lines"].update(field_lines(slot * 12, 12))


def finish_body(body):
    body["unique_publication_slots"] = len(body["_memo"])
    body["unique_query_slots"] = len(body["_query_slots"])
    body["publication_occupancy"] = ratio(body["unique_publication_slots"], body["token_count"])
    for kind in ("query", "write"):
        current = body[f"_current_{kind}_lines"]
        hot = body[f"_hot_cold_{kind}_lines"]
        body[f"current_unique_{kind}_payload_lines"] = len(current)
        body[f"hot_cold_unique_{kind}_payload_lines"] = len(hot)
    body["current_unique_payload_lines_union"] = len(body["_current_query_lines"] | body["_current_write_lines"])
    body["hot_cold_unique_payload_lines_union"] = len(body["_hot_cold_query_lines"] | body["_hot_cold_write_lines"])
    for name in ("miss_reasons_flags_first", "miss_reasons_current_order", "miss_conditions_overlap"):
        body[name] = {reason: body[name][reason] for reason in REASONS}
    for name in list(body):
        if name.startswith("_"):
            del body[name]
    return body


def read_trace(path):
    bodies = []
    body = None
    events = 0
    with path.open("r", encoding="ascii") as handle:
        for line_number, line in enumerate(handle, 1):
            columns = line.split()
            if not columns or columns[0].startswith("#"):
                continue
            location = f"{path.name}:{line_number}"
            kind = columns[0]
            expected_fields = {"B": 4, "Q": 7, "W": 5, "E": 0}.get(kind)
            if expected_fields is None or len(columns) != expected_fields + 1:
                raise ValueError(f"{location}: unknown event or wrong number of fields")
            if any(not field.isascii() or not field.isdecimal() for field in columns[1:]):
                raise ValueError(f"{location}: nondecimal unsigned field")
            fields = [int(field) for field in columns[1:]]
            if any(field > U32_MAX for field in fields):
                raise ValueError(f"{location}: u32 field overflow")
            if kind == "B":
                if body is not None or fields[0] + fields[1] > U32_MAX:
                    raise ValueError(f"{location}: nested body or overflowing absolute range")
                body = new_body(path, len(bodies), *fields)
            else:
                if body is None:
                    raise ValueError(f"{location}: event outside a live body")
                if kind == "Q":
                    lookup(body, fields, location)
                elif kind == "W":
                    publish(body, fields, location)
                else:
                    bodies.append(finish_body(body))
                    body = None
            events += 1
    if body is not None:
        raise ValueError(f"{path.name}: unterminated body at EOF")
    return bodies, events


def ratio(numerator, denominator):
    return numerator / denominator if denominator else 0.0


SUM_KEYS = (
    "token_count", "queries", "root_queries", "nested_queries", "hits", "writes", "overwrites", "mismatches",
    "unique_publication_slots", "unique_query_slots", "scope_gt_u16_publications", "type_gt_u16_publications",
    "scope_gt_u16_queries", "type_count_gt_u16_queries", "scope_count_gt_u16_at_begin", "type_count_gt_u16_at_begin",
    "current_init_bytes", "hot_cold_init_bytes", "current_storage_bytes", "hot_cold_storage_bytes", "narrow_storage_bytes",
    "current_query_payload_probes", "hot_cold_query_payload_probes",
    "current_unique_query_payload_lines", "hot_cold_unique_query_payload_lines",
    "current_unique_write_payload_lines", "hot_cold_unique_write_payload_lines",
    "current_unique_payload_lines_union", "hot_cold_unique_payload_lines_union",
)


def summarize(bodies):
    summary = {key: sum(body[key] for body in bodies) for key in SUM_KEYS}
    summary["bodies"] = len(bodies)
    summary["zero_token_bodies"] = sum(body["token_count"] == 0 for body in bodies)
    summary["narrow_feasible_bodies"] = sum(body["narrow_feasible"] for body in bodies)
    summary["narrow_feasible_queries"] = sum(body["queries"] for body in bodies if body["narrow_feasible"])
    summary["narrow_feasible_body_fraction"] = ratio(summary["narrow_feasible_bodies"], len(bodies))
    summary["narrow_feasible_query_fraction"] = ratio(summary["narrow_feasible_queries"], summary["queries"])
    summary["hit_fraction"] = ratio(summary["hits"], summary["queries"])
    summary["publication_occupancy_weighted"] = ratio(summary["unique_publication_slots"], summary["token_count"])
    for key in ("token_count", "scope_count_at_begin", "type_count_at_begin", "max_scope_id", "max_type_id", "max_query_type_count",
                "current_init_bytes", "hot_cold_init_bytes", "current_storage_bytes", "hot_cold_storage_bytes", "narrow_storage_bytes"):
        summary[f"peak_{key}"] = max((body[key] for body in bodies), default=0)
    for key in ("miss_reasons_flags_first", "miss_reasons_current_order", "miss_conditions_overlap"):
        summary[key] = {reason: sum(body[key][reason] for body in bodies) for reason in REASONS}
    return summary


def summary_row(name, summary):
    print(f"{name}\t{summary['bodies']}\t{summary['token_count']}\t{summary['queries']}\t"
          f"{summary['hit_fraction'] * 100:.2f}\t{summary['publication_occupancy_weighted'] * 100:.2f}\t"
          f"{summary['writes']}\t{summary['overwrites']}\t{summary['current_init_bytes']}\t"
          f"{summary['hot_cold_init_bytes']}\t{summary['narrow_feasible_query_fraction'] * 100:.2f}\t{summary['mismatches']}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("artifact_dir", type=Path)
    args = parser.parse_args()
    paths = sorted(args.artifact_dir.rglob("*.trace"))
    if not paths:
        parser.error("no *.trace files found")
    all_bodies = []
    trace_summaries = []
    events = 0
    try:
        for path in paths:
            bodies, event_count = read_trace(path)
            summary = summarize(bodies)
            summary["trace"] = str(path.relative_to(args.artifact_dir))
            summary["events"] = event_count
            trace_summaries.append(summary)
            all_bodies.extend(bodies)
            events += event_count
    except (OSError, UnicodeError, ValueError) as error:
        print(str(error), file=sys.stderr)
        return 1

    overall = summarize(all_bodies)
    buckets = {bucket: summarize([body for body in all_bodies if body["bucket"] == bucket]) for bucket in BUCKETS}
    output = {
        "schema": "expression-memo-census-v1",
        "events": events,
        "model_notes": [
            "No timings, cache-miss claims, production performance acceptance, or permanent IR.",
            "Publication occupancy is distinct W slots / body token slots; overwrite does not increase occupancy.",
            "Flags-first miss partition precedence is empty, mode, end, scope, type; overlap counts are intentionally nonexclusive.",
            "Current-order misses follow production end, scope, flags, type; empty entries ordinarily stop at end.",
            "Unique payload lines follow current end-first reads versus hot/cold flags-first reads; flag lines excluded.",
            "Line counts use conceptual separately 64-byte-aligned populations, aggregate unique lines per body, not cache misses.",
            "Storage sums and per-body peaks exclude arena retention, other scratch, lane concurrency, and process RSS.",
            "Narrow feasibility means body tokens <=65535; global scope/type IDs are never narrowed.",
            "scope/type_gt_u16_publications count publication events, not distinct IDs; count >65535 does not prove used IDs overflow.",
        ],
        "overall": overall,
        "traces": trace_summaries,
        "population_buckets": buckets,
        "bodies": all_bodies,
    }
    destination = args.artifact_dir / "census.json"
    destination.write_text(json.dumps(output, indent=2) + "\n", encoding="utf-8")
    print("population\tbodies\ttokens\tqueries\thit_pct\toccupied_pct\twrites\toverwrites\tcurrent_zero_bytes\thot_zero_bytes\tnarrow_query_pct\tmismatches")
    summary_row("ALL", overall)
    for summary in trace_summaries:
        summary_row(summary["trace"], summary)
    for bucket, summary in buckets.items():
        summary_row(f"bucket:{bucket}", summary)
    print(f"# wrote {destination}")
    return int(overall["mismatches"] != 0)


if __name__ == "__main__":
    sys.exit(main())
