#!/usr/bin/env python3
"""Summarize a #531 provenance census (rows written by provenance-probe.patch).

usage: analyze.py CASE.tsv [--history OLD_CENSUS.tsv] [--lanes 2,4,8]

Every number printed here is a deterministic count taken from one serial
compile. With --history, the per-body nanoseconds of the 2026-09-23T145513Z
census (same c_gen.c blob, measured on a 9700X, NOT a current or admitted
timing) are joined by function name to weight the same classification; the
join coverage is printed and the result is labelled historical.
"""
import argparse
import collections
import heapq
import sys

SITES = ("s_ptr", "s_array", "s_qual", "s_atomic", "s_aligned", "s_scalar", "s_literal", "s_import", "s_static", "s_compound",
         "s_cleanup_sym", "s_cleanup_fn")
HITS = ("h_ptr_x", "h_array_x", "h_qual_x", "h_import_x", "h_entity_x", "h_cleanup_x")
ROWS = ("r_type_x", "r_sym_x", "t_type_x", "t_sym_x", "t_global_x")


def read_rows(path):
    header = None
    rows = []
    program = None
    with open(path, encoding="utf-8") as handle:
        for line in handle:
            parts = line.rstrip("\n").split("\t")
            if parts[0] == "H":
                header = parts[1:]
            elif parts[0] == "F":
                assert header, "F row before header"
                row = dict(zip(header, parts[1:]))
                for key, value in row.items():
                    if key not in ("name", "status"):
                        row[key] = int(value)
                rows.append(row)
            elif parts[0] == "P":
                program = parts[1:]
    return rows, program


def read_history(path):
    history = collections.defaultdict(list)
    with open(path, encoding="utf-8") as handle:
        for line in handle:
            parts = line.rstrip("\n").split("\t")
            if parts[0] == "F" and parts[-1] == "ok":
                history[parts[2]].append((int(parts[3]), int(parts[8])))  # (body_tokens, total_ns)
    return history


def publishes(row):
    return row["d_types"] + row["d_symbols"] + row["d_globals"] + row["d_functions"] > 0


def publishes_non_type(row):
    return row["d_symbols"] + row["d_globals"] + row["d_functions"] > 0


def consumes_cross(row):
    return sum(row[key] for key in HITS) + sum(row[key] for key in ROWS) > 0


def fills_shared_memo(row):
    return row["fill_ptr"] + row["fill_array"] + row["abi_miss"] > 0


def schedule(durations, lanes):
    """Greedy list scheduling in the given order (an atomic take-index): makespan."""
    if not durations:
        return 0
    heap = [0] * lanes
    for duration in durations:
        earliest = heapq.heappop(heap)
        heapq.heappush(heap, earliest + duration)
    return max(heap)


def share(part, whole):
    return f"{100.0 * part / whole:.1f}%" if whole else "n/a"


def summarize(rows, program, history, lane_counts):
    ok = [row for row in rows if row["status"] == "ok"]
    print(f"bodies: {len(rows)} rows; ok={len(ok)}; other statuses: "
          + ", ".join(f"{status}={count}" for status, count in collections.Counter(row['status'] for row in rows if row['status'] != 'ok').items()))
    if program:
        print(f"program: preamble types/symbols/globals/functions = {program[1]}/{program[3]}/{program[5]}/{program[7]}; "
              f"final = {program[2]}/{program[4]}/{program[6]}/{program[8]}; declarations={program[9]} lowered={program[10]} rejected={program[11]}")
    tokens = sum(row["body_tokens"] for row in ok)
    instructions = sum(row["instructions"] for row in ok)
    timed = {}
    if history is not None:
        for row in ok:
            candidates = history.get(row["name"], [])
            exact = [ns for (tok, ns) in candidates if tok == row["body_tokens"]]
            if len(exact) == 1:
                timed[row["decl"]] = exact[0]
            elif len(candidates) == 1:
                timed[row["decl"]] = candidates[0][1]
        joined_ns = sum(timed.values())
        print(f"historical timing join: {len(timed)}/{len(ok)} ok bodies matched by (name, body_tokens) or unique name; "
              f"{joined_ns / 1e6:.1f} ms of historical body time joined")

    def weights(subset):
        n = len(subset)
        t = sum(row["body_tokens"] for row in subset)
        i = sum(row["instructions"] for row in subset)
        h = sum(timed.get(row["decl"], 0) for row in subset) if timed else None
        line = f"{n:6d} bodies ({share(n, len(ok))}), {t:9d} tokens ({share(t, tokens)}), {i:9d} instrs ({share(i, instructions)})"
        if h is not None:
            line += f", historical {h / 1e6:8.2f} ms ({share(h, sum(timed.values()))})"
        return line

    pub = [row for row in ok if publishes(row)]
    pub_non_type = [row for row in ok if publishes_non_type(row)]
    cons = [row for row in ok if consumes_cross(row)]
    memo = [row for row in ok if fills_shared_memo(row)]
    eligible_frozen = [row for row in ok if not publishes(row) and not consumes_cross(row)]
    eligible_types_private = [row for row in ok if not publishes_non_type(row) and not consumes_cross(row)]
    zero_delta = [row for row in ok if not publishes(row)]
    zero_delta_but_consuming = [row for row in zero_delta if consumes_cross(row)]
    print("\n== classification of ok bodies")
    print("publishes any shared entity        :", weights(pub))
    print("publishes symbol/global/function   :", weights(pub_non_type))
    print("consumes a body-published entity   :", weights(cons))
    print("zero-delta (old 'pure' criterion)  :", weights(zero_delta))
    print("  ...of which consume cross-body   :", weights(zero_delta_but_consuming))
    print("fills a shared lazy memo           :", weights(memo))
    print("ELIGIBLE frozen prefix (no publish, no cross consume):", weights(eligible_frozen))
    print("ELIGIBLE types-private (no sym/global publish, no cross consume):", weights(eligible_types_private))
    print("  frozen-eligible bodies that still fill a shared memo:", weights([row for row in eligible_frozen if fills_shared_memo(row)]))

    print("\n== publication sites (sum over ok bodies; bodies with site > 0)")
    for site in SITES:
        total = sum(row[site] for row in ok)
        bodies = sum(1 for row in ok if row[site] > 0)
        if total:
            print(f"  {site:14s} total={total:7d} bodies={bodies:5d}")
    other_types = sum(row["d_types"] for row in ok) - sum(row[s] for row in ok for s in ("s_ptr", "s_array", "s_qual", "s_atomic", "s_aligned", "s_scalar", "s_literal", "s_import"))
    print(f"  d_types total={sum(row['d_types'] for row in ok)} d_symbols={sum(row['d_symbols'] for row in ok)} "
          f"d_globals={sum(row['d_globals'] for row in ok)} d_functions={sum(row['d_functions'] for row in ok)}; "
          f"types not attributed to a hooked site (upper bound; literal/import types are counted through their symbol site): {other_types}")
    combos = collections.Counter(tuple(site for site in SITES if row[site] > 0) for row in pub)
    print("  publisher site combinations (bodies):")
    for combo, count in combos.most_common(12):
        print(f"    {count:5d}  {'+'.join(combo) if combo else '(unattributed)'}")

    print("\n== cross-body consumption channels (sum over ok bodies; bodies with channel > 0)")
    for key in HITS + ROWS:
        total = sum(row[key] for row in ok)
        bodies = sum(1 for row in ok if row[key] > 0)
        if total:
            print(f"  {key:12s} total={total:8d} bodies={bodies:5d}")
    prefix_hits = sum(row["h_type_prefix"] + row["h_symbol_prefix"] for row in ok)
    self_hits = sum(row["h_type_self"] + row["h_symbol_self"] for row in ok)
    cross_hits = sum(row[key] for row in ok for key in HITS)
    print(f"  lookup hits by origin: prefix={prefix_hits} self={self_hits} cross={cross_hits}")
    distances = [row["decl"] - row["max_producer"] for row in cons if row["max_producer"] >= 0]
    if distances:
        distances.sort()
        print(f"  consumer -> nearest producer distance (declarations): min={distances[0]} p50={distances[len(distances)//2]} max={distances[-1]}")
    label = max((row["label_relocs"] for row in rows), default=0)
    print(f"  label-address relocations at loop end: {label}")
    print(f"  shared memo fills: fill_ptr={sum(row['fill_ptr'] for row in ok)} fill_array={sum(row['fill_array'] for row in ok)} "
          f"abi_miss={sum(row['abi_miss'] for row in ok)} (bodies with abi_miss>0: {sum(1 for row in ok if row['abi_miss'] > 0)})")

    if timed:
        print("\n== MODELED makespans from HISTORICAL per-body ns (zero overhead, declaration-order take-index; not a forecast)")
        serial = sum(timed.values())
        ordered = [row for row in ok if row["decl"] in timed]
        durations = [timed[row["decl"]] for row in ordered]
        largest = max(durations)
        print(f"  serial loop (joined bodies) = {serial / 1e6:.1f} ms; largest body = {largest / 1e6:.2f} ms")
        frozen_set = {row["decl"] for row in eligible_frozen}
        types_set = {row["decl"] for row in eligible_types_private}
        for lanes in lane_counts:
            all_parallel = schedule(durations, lanes)
            frozen_par = schedule([timed[row["decl"]] for row in eligible_frozen if row["decl"] in timed], lanes)
            frozen_replay = sum(ns for decl, ns in timed.items() if decl not in frozen_set)
            types_par = schedule([timed[row["decl"]] for row in eligible_types_private if row["decl"] in timed], lanes)
            types_replay = sum(ns for decl, ns in timed.items() if decl not in types_set)
            print(f"  lanes={lanes}: all-bodies-parallel (private view + ordered merge, merge cost excluded) = {all_parallel / 1e6:.1f} ms; "
                  f"frozen-prefix deferral = {frozen_par / 1e6:.1f} ms parallel + {frozen_replay / 1e6:.1f} ms serial replay "
                  f"(+0..{frozen_replay / 1e6:.1f} ms aborted attempts); "
                  f"types-private deferral = {types_par / 1e6:.1f} + {types_replay / 1e6:.1f} ms")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("tsv")
    parser.add_argument("--history", default=None)
    parser.add_argument("--lanes", default="2,4,8")
    args = parser.parse_args()
    rows, program = read_rows(args.tsv)
    history = read_history(args.history) if args.history else None
    print(f"# {args.tsv}")
    summarize(rows, program, history, [int(value) for value in args.lanes.split(",")])
    return 0


if __name__ == "__main__":
    sys.exit(main())
