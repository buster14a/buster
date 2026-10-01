#!/usr/bin/env python3
"""Offline x86 rel8/rel32 layout oracle; independent of Buster emit code.

No production changes/dependencies. Exhaustive encoding enumeration is bounded
to each small case. Targets are labels at block boundaries; no addends, fixed
blob sizes, no linker relocations, no branch elimination/reordering.
"""
import itertools
import json
import argparse
from pathlib import Path

SHORT_RANGE = (-128, 127)
LONG_RANGE = (-(1 << 31), (1 << 31) - 1)


def layout(items, mask):
    labels, branches, offset = {}, [], 0
    for kind, payload in items:
        if kind == 'label':
            labels[payload] = offset
        elif kind == 'bytes':
            offset += payload
        elif kind == 'align':
            offset = (offset + payload - 1) // payload * payload
        elif kind == 'branch':
            target, long_size = payload
            index = len(branches)
            size = long_size if (mask >> index) & 1 else 2
            branches.append((offset, size, target))
            offset += size
        else:
            raise ValueError(kind)
    displacements = [labels[target] - source - size
                     for source, size, target in branches]
    legal = all((LONG_RANGE if (mask >> i) & 1 else SHORT_RANGE)[0] <= disp <=
                (LONG_RANGE if (mask >> i) & 1 else SHORT_RANGE)[1]
                for i, disp in enumerate(displacements))
    return offset, legal, displacements


def exhaustive(items, n):
    candidates = [(layout(items, mask)[0], mask)
                  for mask in range(1 << n) if layout(items, mask)[1]]
    # Stable bit order is stream order; mask tie-break is explicit.
    return min(candidates) if candidates else None


def widen(items, n):
    """Start shortest; simultaneously promote every currently invalid short.

    Always bounded by n promotions + one final check, including with alignment.
    Final validity is checked. Least solution/min-size only without alignment.
    """
    mask, rounds = 0, 0
    while True:
        rounds += 1
        size, legal, displacements = layout(items, mask)
        promote = sum(1 << i for i, d in enumerate(displacements)
                      if not ((mask >> i) & 1) and not -128 <= d <= 127)
        if not promote:
            return (size, mask, legal, rounds)
        mask |= promote


def guarded_single_pass(items, n):
    """Cheap bounded alternative for current MIR contract: no internal align.

    Compute all-long offsets once; choose short using prospective own length.
    For forward boundary labels own shortening cancels target/source movement;
    for backward labels the shorter PC origin brings the delta toward zero.
    A full final validity check is retained. Not an optimum algorithm.
    """
    mask = (1 << n) - 1
    _, _, displacements = layout(items, mask)
    branch_items = [payload for kind, payload in items if kind == 'branch']
    for i, (disp, (_, long_size)) in enumerate(zip(displacements, branch_items)):
        prospective = disp if disp >= 0 else disp + long_size - 2
        if -128 <= prospective <= 127:
            mask &= ~(1 << i)
    size, legal, _ = layout(items, mask)
    return size, mask, legal


def greedy_checked_shrink(items, n):
    """Alignment-safe bounded fallback: trial each shrink in stream order;
    commit only when complete resulting layout is legal. One trial per branch.
    No optimality guarantee. O(n*(items+n)), O(items+n) scratch.
    """
    mask = (1 << n) - 1
    for i in range(n):
        trial = mask & ~(1 << i)
        if layout(items, trial)[1]:
            mask = trial
    size, legal, _ = layout(items, mask)
    return size, mask, legal


def describe(items, n):
    best = exhaustive(items, n)
    all_long = (1 << n) - 1
    return {'items': items, 'all_long': [all_long, *layout(items, all_long)],
            'oracle': best, 'widen': widen(items, n),
            'single_pass': guarded_single_pass(items, n),
            'greedy': greedy_checked_shrink(items, n)}


def case_family(pads, n, alignment=None, align_position=0):
    for gaps in itertools.product(pads, repeat=n):
        for targets in itertools.product(range(n + 1), repeat=n):
            for long_sizes in itertools.product((5, 6), repeat=n):
                items = []
                for i in range(n):
                    items += [('label', i), ('bytes', gaps[i]),
                              ('branch', (targets[i], long_sizes[i]))]
                    if alignment and align_position == i:
                        items += [('align', alignment)]
                items += [('label', n)]
                yield items


def run(output_path):
    results = {'model': 'fixed stream, rel8/rel32 end-PC, boundary labels, no addends'}
    pads = (0, 1, 2, 120, 121, 122, 123, 124, 125, 126, 127, 128, 129, 130)
    count = oracle_choices = nonoptimal_single = savings_long = savings_single = 0
    first_nonoptimal = None
    max_rounds = 0
    for items in itertools.chain(case_family(pads, 2),
                                 case_family((0, 1, 120, 124, 127, 128), 3)):
        n = sum(kind == 'branch' for kind, _ in items)
        count += 1
        oracle_choices += 1 << n
        best = exhaustive(items, n)
        w = widen(items, n)
        s = guarded_single_pass(items, n)
        assert best is not None and w[2] and s[2]
        assert w[0] == best[0], describe(items, n)
        max_rounds = max(max_rounds, w[3])
        nonoptimal_single += s[0] != best[0]
        if s[0] != best[0] and first_nonoptimal is None:
            first_nonoptimal = describe(items, n)
        all_size = layout(items, (1 << n) - 1)[0]
        savings_long += all_size - best[0]
        savings_single += all_size - s[0]
    results['no_alignment'] = {
        'layouts': count, 'encoding_assignments': oracle_choices,
        'illegal_widen': 0, 'nonoptimal_widen': 0,
        'max_widen_rounds': max_rounds, 'illegal_single_pass': 0,
        'nonoptimal_single_pass': nonoptimal_single,
        'summed_oracle_savings_bytes': savings_long,
        'summed_single_pass_savings_bytes': savings_single,
        'first_single_pass_nonoptimal': first_nonoptimal}

    # Real x86 threshold, one explicit alignment hazard for naive shrink.
    hazard = [('label', 0), ('bytes', 123), ('branch', (1, 6)),
              ('bytes', 4), ('align', 128), ('label', 1)]
    results['single_branch_alignment_hazard'] = describe(hazard, 1)
    assert layout(hazard, 1)[1] and not layout(hazard, 0)[1]

    # Both short branches are initially out of range. Widening only the first
    # moves label 1 forward by 3, absorbed by alignment before branch 2; branch
    # 2 then fits rel8. Simultaneous promotions retain 3 unnecessary end bytes.
    overpromotion = [('label', 0), ('branch', (2, 5)), ('bytes', 127),
                     ('label', 1), ('align', 256), ('branch', (1, 5)),
                     ('label', 2)]
    results['widen_alignment_nonoptimal'] = describe(overpromotion, 2)
    assert widen(overpromotion, 2)[0] == 261
    assert exhaustive(overpromotion, 2) == (258, 1)

    # Exact no-encoding case uses a sparse byte count, not a 2-GiB allocation.
    # Neither rel8 nor rel32 can reach. Same representability policy as the
    # concrete MIR path: bounded failure, no thunk or apparent success.
    impossible = [('label', 0), ('branch', (1, 5)),
                  ('bytes', 1 << 31), ('label', 1)]
    results['no_supported_encoding'] = describe(impossible, 1)
    assert exhaustive(impossible, 1) is None
    assert not widen(impossible, 1)[2]

    count = illegal_single = nonoptimal_widen = nonoptimal_greedy = 0
    first_widen = first_greedy = first_illegal = None
    max_rounds = 0
    align_pads = (0, 1, 2, 3, 4, 5, 120, 121, 122, 123, 124, 125, 126, 127, 128, 129, 130, 131, 132)
    for align in (8, 16, 32, 128, 256):
        for position in (0, 1):
            for items in case_family(align_pads, 2, align, position):
                count += 1
                best = exhaustive(items, 2)
                w = widen(items, 2)
                s = guarded_single_pass(items, 2)
                g = greedy_checked_shrink(items, 2)
                assert best is not None and w[2] and g[2]
                max_rounds = max(max_rounds, w[3])
                illegal_single += not s[2]
                nonoptimal_widen += w[0] != best[0]
                nonoptimal_greedy += g[0] != best[0]
                if not s[2] and first_illegal is None:
                    first_illegal = describe(items, 2)
                if w[0] != best[0] and first_widen is None:
                    first_widen = describe(items, 2)
                if g[0] != best[0] and first_greedy is None:
                    first_greedy = describe(items, 2)
    results['alignment'] = {
        'layouts': count, 'encoding_assignments': count * 4,
        'illegal_single_pass': illegal_single,
        'illegal_widen': 0, 'nonoptimal_widen': nonoptimal_widen,
        'max_widen_rounds': max_rounds,
        'illegal_greedy': 0, 'nonoptimal_greedy': nonoptimal_greedy,
        'first_illegal_single_pass': first_illegal,
        'first_widen_nonoptimal': first_widen,
        'first_greedy_nonoptimal': first_greedy}
    output = Path(output_path)
    output.write_text(json.dumps(results, indent=2) + '\n')
    print(json.dumps(results, indent=2))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', required=True)
    run(parser.parse_args().output)
