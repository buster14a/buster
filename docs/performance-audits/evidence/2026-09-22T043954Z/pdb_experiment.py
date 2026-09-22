#!/usr/bin/env python3
"""Independent map-only model for stable PDB merge transcripts.

This tests neither record rewriting nor hashing nor PDB bytes. It deliberately
models arbitrary stable equality partitions, not a favorable hash function.
"""
import hashlib
import json
import random
from pathlib import Path

BIAS = 0x1000
LIMIT = 8
POISON = object()


def partitions(n):
    """All restricted-growth strings: stable first-encounter class numbering."""
    if n == 0:
        yield ()
    else:
        def visit(prefix, maximum):
            if len(prefix) == n:
                yield tuple(prefix)
            else:
                for value in range(maximum + 2):
                    yield from visit(prefix + [value], max(maximum, value))
        yield from visit([0], 0)


def check(n, transcript):
    live = list(range(n))
    reference = [BIAS + i for i in range(n)]
    candidate = [POISON] * n
    assigned = [0] * n
    reference_writes = n
    parent_writes = 0
    merging_rounds = 0
    for groups in transcript[:LIMIT]:
        if not live:
            break
        assert len(groups) == len(live)
        survivors = []
        for position, group in enumerate(groups):
            if group == len(survivors):
                survivors.append(live[position])
            else:
                assert 0 <= group < len(survivors)
                child, parent = live[position], survivors[group]
                assert parent < child
                assert candidate[child] is POISON
                candidate[child] = BIAS + parent
                assigned[child] += 1
                parent_writes += 1
        if len(survivors) == len(live):
            break
        merging_rounds += 1
        reference = [BIAS + groups[value - BIAS] for value in reference]
        reference_writes += n
        live = survivors
    cursor = 0
    for original in range(n):
        if cursor < len(live) and original == live[cursor]:
            assert candidate[original] is POISON
            candidate[original] = BIAS + cursor
            cursor += 1
        else:
            assert assigned[original] == 1
            parent = candidate[original] - BIAS
            assert 0 <= parent < original
            assert candidate[parent] is not POISON
            candidate[original] = candidate[parent]
    assert cursor == len(live)
    assert candidate == reference
    assert parent_writes == n - len(live)
    return dict(n=n, final_count=len(live), merging_rounds=merging_rounds,
                reference_map_writes=reference_writes,
                proposed_map_writes=n + parent_writes)


def all_transcripts(n, prefix=()):
    if n == 0 or len(prefix) == LIMIT:
        yield prefix
    else:
        for groups in partitions(n):
            count = max(groups, default=-1) + 1
            if count == n:
                yield prefix + (groups,)
            else:
                yield from all_transcripts(count, prefix + (groups,))


def run():
    exhaustive = {}
    for n in range(7):
        count = 0
        for transcript in all_transcripts(n):
            check(n, transcript)
            count += 1
        exhaustive[n] = count
    seed = 0xB057E2026
    rng = random.Random(seed)
    random_count = 20000
    maximum_rounds = 0
    for _ in range(random_count):
        n = rng.randrange(513)
        live_count = n
        transcript = []
        for _round in range(LIMIT):
            if not live_count:
                break
            groups = []
            count = 0
            probability = rng.choice((0.02, 0.1, 0.5, 0.9))
            for _position in range(live_count):
                if count and rng.random() < probability:
                    groups.append(rng.randrange(count))
                else:
                    groups.append(count)
                    count += 1
            transcript.append(tuple(groups))
            if count == live_count:
                break
            live_count = count
        result = check(n, tuple(transcript))
        maximum_rounds = max(maximum_rounds, result['merging_rounds'])
    # Every round retires the newest surviving ordinal into its predecessor;
    # the initial last record ends with eight parent links, all resolved by the
    # one ascending final pass. The eight-round production cap remains active.
    n = 128
    chain = tuple(tuple(list(range(k - 1)) + [k - 2]) for k in range(n, n - LIMIT, -1))
    controls = {
        'empty': check(0, ()),
        'no_merges': check(16, (tuple(range(16)),)),
        'one_round_all_equal': check(128, ((0,) * 128, (0,))),
        'eight_round_retiring_root_chain': check(n, chain),
    }
    output = dict(evidence='map-equivalence correctness model; no timing or PDB-output test',
                  repository_commit='ec1897b900c694e57a10ee5ddaacd89c4cd7c57c',
                  repository_tree='1e20ddfb534ba16a05a277136ff4f391a93dc28f',
                  exhaustive_transcripts_by_n=exhaustive,
                  exhaustive_total=sum(exhaustive.values()),
                  randomized_transcripts=random_count, random_seed=seed,
                  maximum_random_merging_rounds=maximum_rounds,
                  poison_reads=0, mismatches=0, controls=controls,
                  script_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest())
    print(json.dumps(output, indent=2))


if __name__ == '__main__':
    run()
