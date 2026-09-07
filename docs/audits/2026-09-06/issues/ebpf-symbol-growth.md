<!-- buster-audit-2026-09-06:ebpf-symbol-growth -->

**Task for an agent.** Read `AGENTS.md` first. Audited on 2026-09-06 against GitHub `main` at `ee3b5c5022bdddef07840b9d787e76dfb6fe6e8b`. The affected implementation was authenticated by Git blob hash, then tested in a recovered checkout at `6d2bdbf9d19e15be93cb2f09fe0c467ba2fa70d2`. This is focused implementation testing, not a successful full build of current `main`.


## What is wrong

Every `ebpf_add_symbol_record` calls `ebpf_symbol_by_key`, which linearly searches all existing records even when inserting a fresh key. Building N unique symbols performs N(N-1)/2 unsuccessful key comparisons. Numeric symbol keys are already available; scanning the growing record array discards that advantage.

Start at [ebpf_symbol_by_key and ebpf_add_symbol_record](https://github.com/buster14a/buster/blob/ee3b5c5022bdddef07840b9d787e76dfb6fe6e8b/src/buster/lib/compiler/ebpf/ebpf.c#L570-L594). Also inspect `ir_symbol_keys`, synthetic string symbols, duplicate references, relocation resolution, and final ELF index assignment.

## Reproduction / measurement

The supplied `evidence/ebpf_symbols.c` invokes the actual insertion helper on fresh contexts. On this virtualized Linux host, one illustrative run produced:

| Unique keys | Comparisons implied by source | Helper elapsed microseconds |
|---:|---:|---:|
| 2,048 | 2,096,128 | 3,658 |
| 4,096 | 8,386,560 | 5,966 |
| 8,192 | 33,550,336 | 16,765 |
| 16,384 | 134,209,536 | 63,632 |

The comparison counts are derived from the control flow, not PMU counters or an instrumented count. The elapsed values are one unpinned microbenchmark run, not a reliable speedup estimate, not an end-to-end compiler result, and not Zen 5 evidence. The exact asymptotic defect does not depend on those timings.

## Requested work

Use a key-to-record-index projection or a compact map, with a clear ownership and invalidation contract. Store indices rather than raw pointers when the record vector can reallocate. Preserve idempotent insertion for repeated keys, stable ELF symbol ordering, and synthetic-symbol handling.

Measure the whole eBPF object-emission workload as well as symbol-heavy cases. Do not replace this with a SIMD full-table scan: deleting the repeated work is the first experiment.

## Definition of done

Insertion cost is linear or expected-linear for unique keys. Duplicate keys reuse the correct record. Symbol and relocation outputs remain byte-identical where ordering is unchanged. Tests cover empty tables, growth, many globals/functions, synthetic strings, and repeated references. Report compiler elapsed time, memory, and index-building overhead; run eBPF tests, `test_all`, and Release self-host.

## Existing work / scope

This concerns eBPF symbol-record construction, not #102's stack-slot allocation and not #117's ELF dynamic-object data lookup. No matching eBPF symbol-growth report appeared in the issue search. No performance patch is proposed here because a correct data-structure migration and an end-to-end benchmark exceed a two-line fix.
