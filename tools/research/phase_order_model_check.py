#!/usr/bin/env python3
"""Offline deterministic agenda reconciliation; does not execute Buster."""
import json
import random

from phase_order_worklist import dense_model, ordered_agenda_model, fifo_model


def main():
    seed = 20260930
    rng = random.Random(seed)
    mismatches = []
    for case in range(10000):
        count = rng.randrange(0, 25)
        blocks = rng.randrange(1, 8)
        parameters = [
            {"block": rng.randrange(blocks), "value": 100 + i,
             "incoming": [rng.choice([0, 1, 2] + list(range(100, 100 + count)))
                          for _ in range(rng.randrange(0, 5))]}
            for i in range(count)
        ]
        parameters.sort(key=lambda p: p["block"])
        dense = dense_model(parameters, blocks)
        agenda = ordered_agenda_model(parameters, blocks)
        if (dense["state"] != agenda["state"] or
                dense["counts"]["logical_sweeps"] != agenda["counts"]["logical_sweeps"] or
                dense["counts"]["cap_hit"] != agenda["counts"]["cap_hit"]):
            mismatches.append(case)
    chain = [{"block": 0, "value": 100 + i,
              "incoming": [0, 101 + i if i < 4 else 0]} for i in range(5)]
    dense = dense_model(chain, 1)
    agenda = ordered_agenda_model(chain, 1)
    fifo = fifo_model(chain, 1)
    result = {
        "command": "python3 tools/research/phase_order_model_check.py",
        "seed": seed, "dependency_graphs": 10000,
        "dense_ordered_mismatch_cases": mismatches,
        "canonical_five_parameter_chain": {"dense": dense, "ordered_agenda": agenda, "fifo": fifo},
        "scope": "Offline dependency graphs, not compiled C or validated canonical IR inputs",
    }
    print(json.dumps(result, indent=2, sort_keys=True))
    return int(bool(mismatches) or dense["state"] != agenda["state"] or
               dense["state"]["remaining_values"] != [100] or
               fifo["state"]["remaining_values"] != [] or
               fifo["counts"]["parameter_visits"] != 9)


if __name__ == "__main__":
    raise SystemExit(main())
