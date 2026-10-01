#!/usr/bin/env python3
"""Offline parameter-cleanup models from isolated PHASE_PARAM diagnostic logs.

This does not run Buster or report compiler time. Dense replay follows the
implemented four-sweep algorithm. The ordered dirty agenda preserves its
current/next ordinal semantics; the optional FIFO is deliberately a different
policy with a four-times-initial-parameter check budget. Reverse-index building,
notifications, retargeting, and queue operations are reported separately.
"""

import argparse
import collections
import hashlib
import heapq
import json
from pathlib import Path
import re
import sys


LOGICAL_SWEEP_CAP = 4
RECORD = re.compile(r"^PHASE_(FUNCTION|PARAM|STEP|DONE)\s+(.*)$")
FIELD = re.compile(r"(\w+)=([^\s]*)")


def root_of(replacements, value, counts=None):
    path = []
    current = value
    while replacements.get(current, current) != current:
        path.append(current)
        current = replacements[current]
    for item in path:
        replacements[item] = current
    if counts is not None:
        counts["root_queries"] += 1
        counts["root_hops"] += len(path)
    return current


def check_parameter(parameter, replacements, counts):
    counts["parameter_visits"] += 1
    same = None
    trivial = True
    for incoming in parameter["incoming"]:
        counts["incoming_visits"] += 1
        value = root_of(replacements, incoming, counts)
        if value != parameter["value"]:
            trivial = same is None or value == same
            same = value
            if not trivial:
                break
    return same if trivial else None


def final_state(parameters, live, replacements, events):
    return {
        "remaining_values": [p["value"] for ordinal, p in enumerate(parameters) if live[ordinal]],
        "parameter_roots": [[p["value"], root_of(replacements, p["value"])] for p in parameters],
        "remaining_incoming": [
            [p["value"], [root_of(replacements, value) for value in p["incoming"]]]
            for ordinal, p in enumerate(parameters) if live[ordinal]
        ],
        "removal_events": events,
    }


def dense_model(parameters, blocks):
    replacements = {}
    live = [True] * len(parameters)
    counts = collections.Counter()
    events = []
    changed = True
    sweep = 0
    while changed and sweep < LOGICAL_SWEEP_CAP:
        sweep += 1
        changed = False
        counts["logical_sweeps"] += 1
        if blocks is not None:
            counts["block_visits"] += blocks
            occupied = len({p["block"] for i, p in enumerate(parameters) if live[i]})
            counts["empty_block_visits"] += blocks - occupied
        for ordinal, parameter in enumerate(parameters):
            if not live[ordinal]:
                continue
            replacement = check_parameter(parameter, replacements, counts)
            if replacement is not None:
                replacements[parameter["value"]] = replacement
                live[ordinal] = False
                counts["removed_parameters"] += 1
                events.append([sweep, parameter["value"], replacement])
                changed = True
    counts["cap_hit"] = int(changed)
    result = {"counts": dict(counts), "state": final_state(parameters, live, replacements, events)}
    if blocks is None:
        result["unavailable"] = ["block and empty-block visits: no PHASE_FUNCTION blocks field"]
    return result


def reverse_index(parameters, blocks, counts):
    reverse = collections.defaultdict(list)
    if blocks is not None:
        counts["construction_block_visits"] += blocks
        counts["construction_empty_block_visits"] += blocks - len({p["block"] for p in parameters})
    counts["construction_parameter_visits"] += len(parameters)
    for ordinal, parameter in enumerate(parameters):
        for incoming_ordinal, value in enumerate(parameter["incoming"]):
            reverse[value].append((ordinal, incoming_ordinal))
            counts["index_creation_incoming_visits"] += 1
            counts["index_creation_occurrences"] += 1
    counts["index_creation_roots"] = len(reverse)
    return reverse


def retarget(reverse, value, replacement, live, counts, notify):
    affected = reverse.pop(value, [])
    counts["index_root_pop_operations"] += 1
    for owner, incoming_ordinal in affected:
        counts["notification_occurrences"] += 1
        reverse[replacement].append((owner, incoming_ordinal))
        counts["index_retarget_occurrences"] += 1
        if live[owner]:
            counts["notification_live_occurrences"] += 1
            notify(owner)
        else:
            counts["notification_stale_occurrences"] += 1


def ordered_agenda_model(parameters, blocks):
    replacements = {}
    live = [True] * len(parameters)
    counts = collections.Counter()
    reverse = reverse_index(parameters, blocks, counts)
    current = []
    current_members = set()
    following = []
    following_members = set()
    events = []

    def enqueue(owner, next_sweep):
        queue, members = (following, following_members) if next_sweep else (current, current_members)
        counts["queue_enqueue_attempts"] += 1
        if owner not in members:
            heapq.heappush(queue, owner)
            members.add(owner)
            counts["queue_enqueues"] += 1
            counts["queue_next_enqueues" if next_sweep else "queue_current_enqueues"] += 1
        else:
            counts["queue_coalesced_attempts"] += 1
        counts["queue_peak_entries"] = max(counts["queue_peak_entries"], len(current) + len(following))

    for ordinal in range(len(parameters)):
        enqueue(ordinal, False)
    changed = True
    sweep = 0
    while changed and sweep < LOGICAL_SWEEP_CAP:
        sweep += 1
        changed = False
        counts["logical_sweeps"] += 1
        while current:
            ordinal = heapq.heappop(current)
            current_members.remove(ordinal)
            counts["queue_pops"] += 1
            if not live[ordinal]:
                counts["queue_stale_pops"] += 1
                continue
            parameter = parameters[ordinal]
            replacement = check_parameter(parameter, replacements, counts)
            if replacement is not None:
                replacements[parameter["value"]] = replacement
                live[ordinal] = False
                counts["removed_parameters"] += 1
                events.append([sweep, parameter["value"], replacement])
                changed = True

                def notify(owner):
                    # An earlier ordinal has already had its dense visit in
                    # this sweep. A later ordinal still gets immediate aliases.
                    enqueue(owner, owner <= ordinal)

                retarget(reverse, parameter["value"], replacement, live, counts, notify)
        if changed and sweep < LOGICAL_SWEEP_CAP:
            current, following = following, []
            current_members, following_members = following_members, set()
            counts["queue_round_transfers"] += 1
        # An empty following queue still charges the unchanged logical round.
    counts["cap_hit"] = int(changed)
    counts["queue_pending_at_exit"] = len(current) + len(following)
    result = {"counts": dict(counts), "state": final_state(parameters, live, replacements, events)}
    if blocks is None:
        result["unavailable"] = ["index-construction block visits: no PHASE_FUNCTION blocks field"]
    return result


def fifo_model(parameters, blocks):
    replacements = {}
    live = [True] * len(parameters)
    counts = collections.Counter()
    reverse = reverse_index(parameters, blocks, counts)
    queue = collections.deque(range(len(parameters)))
    members = set(queue)
    counts["queue_enqueue_attempts"] = len(parameters)
    counts["queue_enqueues"] = len(parameters)
    counts["queue_peak_entries"] = len(parameters)
    budget = LOGICAL_SWEEP_CAP * len(parameters)
    events = []

    def notify(owner):
        counts["queue_enqueue_attempts"] += 1
        if owner not in members:
            queue.append(owner)
            members.add(owner)
            counts["queue_enqueues"] += 1
        else:
            counts["queue_coalesced_attempts"] += 1
        counts["queue_peak_entries"] = max(counts["queue_peak_entries"], len(queue))

    while queue and counts["parameter_visits"] < budget:
        ordinal = queue.popleft()
        members.remove(ordinal)
        counts["queue_pops"] += 1
        if not live[ordinal]:
            counts["queue_stale_pops"] += 1
            continue
        parameter = parameters[ordinal]
        replacement = check_parameter(parameter, replacements, counts)
        if replacement is not None:
            replacements[parameter["value"]] = replacement
            live[ordinal] = False
            counts["removed_parameters"] += 1
            events.append([counts["parameter_visits"], parameter["value"], replacement])
            retarget(reverse, parameter["value"], replacement, live, counts, notify)
    counts["parameter_check_budget"] = budget
    counts["budget_exhausted"] = int(bool(queue))
    counts["queue_pending_at_exit"] = len(queue)
    return {"counts": dict(counts), "state": final_state(parameters, live, replacements, events)}


def parse_fields(text):
    fields = {}
    for key, value in FIELD.findall(text):
        fields[key] = int(value) if value.isdecimal() else value
    return fields


def snapshots_from_log(path, assume_trace):
    raw = path.read_bytes()
    data = raw.decode("utf-8", errors="replace")
    records = []
    for number, line in enumerate(data.splitlines(), 1):
        match = RECORD.match(line)
        if match:
            records.append((number, match[1], parse_fields(match[2])))
    trace_known = assume_trace or any(kind == "PARAM" for _, kind, _ in records)
    functions = []
    snapshots = []
    errors = []
    current = None
    parameters = []
    for line, kind, fields in records:
        if kind == "FUNCTION":
            current = dict(fields, ordinal=len(functions), line=line, parameter_calls=0)
            functions.append(current)
            parameters = []
        elif kind == "PARAM" and current is not None:
            incoming = fields.get("incoming", "")
            incoming = [int(value) for value in str(incoming).split(",") if value]
            parameter = {"block": fields["block"], "value": fields["value"], "incoming": incoming}
            if fields.get("function") != current.get("function"):
                errors.append({"line": line, "error": "parameter/function name mismatch"})
            parameters.append(parameter)
        elif kind == "STEP" and current is not None and fields.get("pass") == 3:
            snapshots.append({
                "function": current.get("function"), "function_ordinal": current["ordinal"],
                "call_ordinal": current["parameter_calls"], "step_ordinal": fields.get("ordinal"),
                "line": line, "blocks": current.get("blocks"), "parameters": parameters,
                "measured": fields, "trace_known": trace_known,
            })
            current["parameter_calls"] += 1
            parameters = []
        elif kind == "DONE":
            current = None
            parameters = []
    return {
        "path": str(path), "sha256": hashlib.sha256(raw).hexdigest(),
        "trace_known": trace_known, "functions": functions, "snapshots": snapshots,
        "errors": errors,
    }


def validate_snapshot(snapshot):
    parameters = snapshot["parameters"]
    errors = []
    values = [p["value"] for p in parameters]
    if len(values) != len(set(values)):
        errors.append("duplicate parameter value IDs in one snapshot")
    if any(p["block"] > parameters[i + 1]["block"] for i, p in enumerate(parameters[:-1])):
        errors.append("parameters are not in ascending block order")
    blocks = snapshot["blocks"]
    if blocks is not None and any(p["block"] >= blocks for p in parameters):
        errors.append("parameter block is outside PHASE_FUNCTION block count")
    return errors


def compare_snapshot(snapshot, include_fifo):
    parameters = snapshot["parameters"]
    dense = dense_model(parameters, snapshot["blocks"])
    agenda = ordered_agenda_model(parameters, snapshot["blocks"])
    result = {key: snapshot[key] for key in (
        "function", "function_ordinal", "call_ordinal", "step_ordinal", "line", "blocks", "measured"
    )}
    result.update(initial_parameters=len(parameters),
                  initial_incoming_occurrences=sum(len(p["incoming"]) for p in parameters),
                  dense=dense, ordered_agenda=agenda)
    mismatches = validate_snapshot(snapshot)
    if dense["state"] != agenda["state"]:
        mismatches.append("ordered agenda final state or ordered removal events differ from dense")
    if dense["counts"]["logical_sweeps"] != agenda["counts"]["logical_sweeps"]:
        mismatches.append("ordered agenda logical sweep count differs from dense")
    measured = snapshot["measured"]
    checks = {
        "visits": dense["counts"].get("parameter_visits", 0) + dense["counts"].get("incoming_visits", 0),
        "changes": dense["counts"].get("removed_parameters", 0),
        "sweeps": dense["counts"]["logical_sweeps"],
    }
    if snapshot["blocks"] is not None:
        checks["block_visits"] = dense["counts"].get("block_visits", 0)
    for key, expected in checks.items():
        if key in measured and measured[key] != expected:
            mismatches.append(f"dense {key}={expected}, PHASE_STEP records {measured[key]}")
    result["mismatches"] = mismatches
    if include_fifo:
        fifo = fifo_model(parameters, snapshot["blocks"])
        result["fifo"] = fifo
        state_keys = ("remaining_values", "parameter_roots", "remaining_incoming")
        result["fifo_same_final_state_as_dense"] = all(
            fifo["state"][key] == dense["state"][key] for key in state_keys
        )
    return result


def input_logs(paths, schedule):
    found = set()
    for path_text in paths:
        path = Path(path_text)
        candidates = sorted(path.rglob("*.log")) if path.is_dir() else [path]
        for candidate in candidates:
            if schedule and schedule not in candidate.parts:
                continue
            found.add(candidate.resolve())
    return sorted(found)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("paths", nargs="+", help="Diagnostic log files or recursively scanned log directories")
    parser.add_argument("--schedule", help="Only paths with this exact schedule directory component, e.g. FADP")
    parser.add_argument("--assume-trace", action="store_true", help="Assert parameter tracing was enabled, even in logs with zero parameters")
    parser.add_argument("--fifo", action="store_true", help="Also model FIFO with at most 4 * initial parameters checks")
    parser.add_argument("--output", type=Path, help="Write JSON here; otherwise print full JSON")
    args = parser.parse_args()
    report = {
        "schema": "buster-phase-order-worklist-v1", "logical_sweep_cap": LOGICAL_SWEEP_CAP,
        "options": {"schedule": args.schedule, "assume_trace": args.assume_trace, "fifo": args.fifo},
        "evidence_class": "offline diagnostic operation counts, not compiler timing or acceptance evidence",
        "ordered_agenda_contract": "Same stable ordinal order and immediate aliases as dense; earlier dirty ordinals wait until next logical sweep",
        "reverse_index_contract": "One initial incoming occurrence row; alias retargeting preserves occurrences; removed owners remain stale and are charged",
        "fifo_contract": "Different policy: deterministic FIFO, at most four times initial parameter count checks; no four-sweep equivalence claim",
        "logs": [], "snapshots": [], "unavailable": [], "errors": [],
    }
    totals = {"dense": collections.Counter(), "ordered_agenda": collections.Counter()}
    if args.fifo:
        totals["fifo"] = collections.Counter()
    incidence = collections.Counter()
    for path in input_logs(args.paths, args.schedule):
        log = snapshots_from_log(path, args.assume_trace)
        if not log["functions"]:
            continue
        report["logs"].append({key: log[key] for key in ("path", "sha256", "trace_known")})
        report["errors"].extend(dict(error, log=str(path)) for error in log["errors"])
        incidence["functions"] += len(log["functions"])
        incidence["functions_without_parameter_call"] += sum(f["parameter_calls"] == 0 for f in log["functions"])
        modeled_functions = collections.defaultdict(list)
        for snapshot in log["snapshots"]:
            if not snapshot["trace_known"]:
                report["unavailable"].append({"log": str(path), "line": snapshot["line"],
                                               "function": snapshot["function"], "reason": "No proof parameter tracing was enabled; use --assume-trace only with known runner configuration"})
                continue
            result = compare_snapshot(snapshot, args.fifo)
            result["log"] = str(path)
            report["snapshots"].append(result)
            modeled_functions[result["function_ordinal"]].append(result)
            incidence["parameter_calls"] += 1
            incidence["parameter_free_calls"] += int(result["initial_parameters"] == 0)
            incidence["calls_with_parameters"] += int(result["initial_parameters"] > 0)
            incidence["calls_with_incoming_index"] += int(result["initial_incoming_occurrences"] > 0)
            incidence["no_match_calls"] += int(result["dense"]["counts"].get("removed_parameters", 0) == 0)
            incidence["changed_calls"] += int(result["dense"]["counts"].get("removed_parameters", 0) > 0)
            incidence["cap_hit_calls"] += result["dense"]["counts"]["cap_hit"]
            incidence["cap_hit_with_remaining_parameters_calls"] += int(
                result["dense"]["counts"]["cap_hit"] and bool(result["dense"]["state"]["remaining_values"])
            )
            incidence["mismatching_calls"] += int(bool(result["mismatches"]))
            for model, counter in totals.items():
                # Queue peaks are maxima, all other operation counts are sums.
                for key, value in result[model]["counts"].items():
                    if key == "queue_peak_entries":
                        counter[key] = max(counter[key], value)
                    else:
                        counter[key] += value
            if args.fifo:
                incidence["fifo_different_final_state_calls"] += int(not result["fifo_same_final_state_as_dense"])
        for calls in modeled_functions.values():
            incidence["modeled_functions"] += 1
            incidence["no_match_functions"] += int(all(c["dense"]["counts"].get("removed_parameters", 0) == 0 for c in calls))
            incidence["parameter_free_functions"] += int(all(c["initial_parameters"] == 0 for c in calls))
    report["incidence"] = dict(incidence)
    report["totals"] = {name: dict(counts) for name, counts in totals.items()}
    if not report["logs"]:
        report["unavailable"].append({"reason": "No PHASE_FUNCTION log matched the inputs/filter"})
    payload = json.dumps(report, indent=2, sort_keys=True) + "\n"
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(payload)
        print(json.dumps({"output": str(args.output), "incidence": report["incidence"],
                          "unavailable": len(report["unavailable"]), "parse_errors": len(report["errors"])}))
    else:
        sys.stdout.write(payload)
    return 1 if incidence["mismatching_calls"] or report["errors"] else 0


if __name__ == "__main__":
    sys.exit(main())
