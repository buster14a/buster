#!/usr/bin/env python3
"""Summarize a #531 lowering census TSV: phase split, per-function distribution,
shared-publication census and modeled (not measured) lane bounds."""
import sys


def pct(values, p):
    if not values:
        return 0
    values = sorted(values)
    index = min(len(values) - 1, int(round(p * (len(values) - 1))))
    return values[index]


def lpt(durations, lanes):
    """Longest-processing-time greedy makespan: a modeled lower-ish bound on
    the parallel loop time with perfect knowledge, zero overhead, zero deferrals."""
    loads = [0] * lanes
    for duration in sorted(durations, reverse=True):
        lane = loads.index(min(loads))
        loads[lane] += duration
    return max(loads)


def source_order_range(durations, lanes):
    """lane_range split of source order (contiguous chunks), zero overhead."""
    count = len(durations)
    loads = []
    for lane in range(lanes):
        start = (count * lane) // lanes
        end = (count * (lane + 1)) // lanes
        loads.append(sum(durations[start:end]))
    return max(loads) if loads else 0


def take_index(durations, lanes):
    """Atomic take-index in source order (dynamic, zero overhead)."""
    loads = [0] * lanes
    for duration in durations:
        lane = loads.index(min(loads))
        loads[lane] += duration
    return max(loads)


def main():
    path = sys.argv[1]
    functions = []
    phase = None
    for line in open(path):
        parts = line.rstrip("\n").split("\t")
        if parts[0] == "F":
            (_, index, name, body_tokens, prepare, body, finish, publish, total, instructions, values, blocks,
             types_added, symbols_added, globals_added, functions_added, arena_bytes, scratch, temporary, ssa, status) = parts
            functions.append(dict(index=int(index), name=name, body_tokens=int(body_tokens), prepare=int(prepare), body=int(body),
                                  finish=int(finish), publish=int(publish), total=int(total), instructions=int(instructions),
                                  values=int(values), blocks=int(blocks), types=int(types_added), symbols=int(symbols_added),
                                  globals=int(globals_added), functions=int(functions_added), arena=int(arena_bytes),
                                  scratch=int(scratch), temporary=int(temporary), ssa=int(ssa), status=status))
        elif parts[0] == "P":
            phase = parts
    if not phase:
        print("no P line")
        return
    (_, source, total, preamble, loop, epilogue, arena_pre, arena_loop, arena_epi, types0, types1, symbols0, symbols1,
     globals0, globals1, functions0, functions1, declarations, lowered, rejected, scratch_hw) = phase
    total = int(total); preamble = int(preamble); loop = int(loop); epilogue = int(epilogue)
    ms = lambda ns: ns / 1e6
    print(f"== {source}")
    print(f"lowering total {ms(total):.3f} ms = preamble {ms(preamble):.3f} ({100*preamble/total:.1f}%) + body loop {ms(loop):.3f} ({100*loop/total:.1f}%) + epilogue {ms(epilogue):.3f} ({100*epilogue/total:.1f}%)")
    print(f"TU arena retained: preamble {int(arena_pre)/2**20:.1f} MiB, body loop {int(arena_loop)/2**20:.1f} MiB, epilogue {int(arena_epi)/2**20:.1f} MiB; per-function scratch high water {int(scratch_hw)/2**20:.1f} MiB")
    print(f"shared tables across body loop: types {types0}->{types1} (+{int(types1)-int(types0)}), symbols {symbols0}->{symbols1} (+{int(symbols1)-int(symbols0)}), globals {globals0}->{globals1} (+{int(globals1)-int(globals0)}), functions {functions0}->{functions1} (+{int(functions1)-int(functions0)})")
    print(f"declarations {declarations}, lowered {lowered}, rejected {rejected}, census rows {len(functions)}")
    ok = [f for f in functions if f["status"] == "ok"]
    statuses = {}
    for f in functions:
        statuses[f["status"]] = statuses.get(f["status"], 0) + 1
    print(f"statuses: {statuses}")
    if not ok:
        return
    totals = [f["total"] for f in ok]
    sum_total = sum(totals)
    sum_prepare = sum(f["prepare"] for f in ok)
    sum_body = sum(f["body"] for f in ok)
    sum_finish = sum(f["finish"] for f in ok)
    sum_publish = sum(f["publish"] for f in ok)
    other_rows = sum(f["total"] for f in functions if f["status"] != "ok")
    print(f"per-function sum {ms(sum_total):.3f} ms (+{ms(other_rows):.3f} ms in non-ok rows); loop overhead outside rows {ms(loop - sum_total - other_rows):.3f} ms")
    print(f"  prepare {ms(sum_prepare):.3f} ({100*sum_prepare/sum_total:.1f}%), body {ms(sum_body):.3f} ({100*sum_body/sum_total:.1f}%), finish(ssa) {ms(sum_finish):.3f} ({100*sum_finish/sum_total:.1f}%), publish {ms(sum_publish):.3f} ({100*sum_publish/sum_total:.1f}%)")
    print(f"per-function total ns: n={len(totals)} mean={sum_total/len(totals):.0f} p50={pct(totals,0.5)} p90={pct(totals,0.9)} p99={pct(totals,0.99)} max={max(totals)}")
    top = sorted(ok, key=lambda f: -f["total"])[:8]
    print("  largest:")
    for f in top:
        print(f"    {f['name']:<48} {ms(f['total']):8.3f} ms  tokens={f['body_tokens']:<7} instr={f['instructions']:<7} share={100*f['total']/sum_total:.2f}%  +types={f['types']} +symbols={f['symbols']} +globals={f['globals']}")
    top_share = top[0]["total"] / sum_total
    tokens = sum(f["body_tokens"] for f in ok)
    instructions = sum(f["instructions"] for f in ok)
    print(f"body tokens {tokens}, instructions {instructions}, ns/token {sum_total/max(1,tokens):.1f}, ns/instruction {sum_total/max(1,instructions):.1f}")
    # publication census
    def share(pred):
        rows = [f for f in ok if pred(f)]
        return len(rows), sum(f["total"] for f in rows)
    for label, pred in [("publishes any shared entity (type|symbol|global|function)", lambda f: f["types"] or f["symbols"] or f["globals"] or f["functions"]),
                        ("appends a type", lambda f: f["types"]), ("appends a symbol", lambda f: f["symbols"]),
                        ("appends a global", lambda f: f["globals"]), ("appends a function", lambda f: f["functions"]),
                        ("appends a type but no symbol/global", lambda f: f["types"] and not (f["symbols"] or f["globals"] or f["functions"]))]:
        n, t = share(pred)
        print(f"  {label}: {n}/{len(ok)} functions ({100*n/len(ok):.1f}%), {ms(t):.3f} ms ({100*t/sum_total:.1f}% of per-function time)")
    print(f"  total appended in bodies: types {sum(f['types'] for f in ok)}, symbols {sum(f['symbols'] for f in ok)}, globals {sum(f['globals'] for f in ok)}, functions {sum(f['functions'] for f in ok)}")
    arena = sum(f["arena"] for f in ok)
    print(f"  TU arena bytes retained by ok bodies {arena/2**20:.1f} MiB (mean {arena/len(ok):.0f} B), scratch max {max(f['scratch'] for f in ok)/2**20:.2f} MiB, temporary-arena delta max {max(f['temporary'] for f in ok)} B")
    ssa = sum(1 for f in ok if f["ssa"] == 1)
    print(f"  direct SSA enabled for {ssa}/{len(ok)} bodies")
    # modeled bounds (NOT forecasts): zero overhead, zero deferrals, perfect knowledge
    print("modeled zero-overhead loop makespan (ms) -- analytical bound from measured serial durations, not a forecast:")
    durations = [f["total"] for f in ok]
    for lanes in (1, 2, 4, 8):
        l = lpt(durations, lanes); r = source_order_range(durations, lanes); t = take_index(durations, lanes)
        print(f"  lanes={lanes}: LPT {ms(l):.3f} (x{sum_total/l:.2f}), lane_range {ms(r):.3f} (x{sum_total/r:.2f}), take-index {ms(t):.3f} (x{sum_total/t:.2f})")
    print(f"  largest single function = {100*top_share:.2f}% of loop => loop cannot drop below {ms(top[0]['total']):.3f} ms at any lane count")
    # deferral model: functions that publish are re-lowered serially (their parallel attempt wasted).
    deferred = [f for f in ok if f["types"] or f["symbols"] or f["globals"] or f["functions"]]
    eligible = [f for f in ok if not (f["types"] or f["symbols"] or f["globals"] or f["functions"])]
    d = sum(f["total"] for f in deferred); e = sum(f["total"] for f in eligible)
    print(f"closed-world model: eligible {len(eligible)} fn / {ms(e):.3f} ms; deferred {len(deferred)} fn / {ms(d):.3f} ms serial replay")
    for lanes in (2, 4, 8):
        par = take_index([f["total"] for f in eligible], lanes)
        modeled_loop = par + d
        print(f"  lanes={lanes}: modeled loop = {ms(par):.3f} parallel + {ms(d):.3f} serial = {ms(modeled_loop):.3f} ms (x{loop/modeled_loop:.2f} vs measured serial loop {ms(loop):.3f}); whole lowering x{total/(total-loop+modeled_loop):.2f}")


if __name__ == "__main__":
    main()
