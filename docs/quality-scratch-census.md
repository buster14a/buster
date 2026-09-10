# QUALITY scratch and work census

This diagnostic owns the first evidence slice of [#125](https://github.com/buster14a/buster/issues/125),
under [#128](https://github.com/buster14a/buster/issues/128). It changes neither
placement nor ordering. The representation candidates (local IDs, generations,
touched lists, sparse traffic/orderings, scalar 64-bit sets and 512-bit tiles)
remain unmeasured hypotheses. No threshold in this report selects an algorithm.

## Collect with the existing measurement system

Build a **separate** compiler with `BUSTER_BENCH_ALLOCATIONS=ON`; ordinary builds
preprocess out the recorder, its TLS storage and extra census walks. Keep the
instrumented compiler out of timing/RSS/PMU trials. The native throughput runner's
existing allocation replays pass `-fsource-metrics`, retain raw metrics and verify
that the diagnostic compiler produces the same object bytes as its ordinary
counterpart. Do not invent a second benchmark launcher.

For an isolated diagnostic command, after the ordinary compiler has been built:

```sh
./build.sh generate --build-directory build-quality-census --cc clang --ci --linker DEFAULT -- -DBUSTER_BENCH_ALLOCATIONS=ON
./build.sh build --build-directory build-quality-census --config Release -t ide -- -j1
build-quality-census/Release/ide cc -g0 -O0 -fregister-allocator=quality -fsource-metrics=quality.metrics -c tests/basic_c_operations.c -o quality.o
```

For paired native-harness measurements, add the already supported
`--allocation-baseline /path/to/baseline-census/ide` and
`--allocation-candidate /path/to/candidate-census/ide` to the ordinary A/B command
in `tools/throughput/README.md`. Both timing subjects must remain uninstrumented.
For this observer-only change, the same ordinary baseline binary may serve as
both diagnostic-replay oracles; record that explicitly as an identity experiment,
not an A/B performance improvement. The prior baseline diagnostic binary will
naturally lack the new keys. Missing keys mean unavailable, not zero.

The keys are additive source-metrics version-1 fields under `quality_census`;
`quality_census.version=1` identifies this counter schema. The native runner keeps
them in `artifacts/*.metrics`; it does not add them to the legacy allocation CSV
columns or derive a new throughput verdict from them. Preserve the run's input
and compiler hashes, commands, host metadata and all raw files.

## Scope and bounds

There are 63 unsigned 64-bit counters: 504 bytes of fixed storage per OS thread.
The snapshot is cumulative on the calling thread, before metrics formatting,
matching the allocation snapshot's existing scope. A single compilation is
currently serial; worker totals are **not** aggregated, and this interface must
be revisited if compiler work moves to persistent lanes. Snapshots allocate
nothing. Instrumented additions fail the process on overflow instead of wrapping
or saturating into an apparently valid report. A failed addition preserves the
counter's previous value. Ordinary self-hosted builds contain no recorder.

Functions skipped before the prepass contribute only entry/fallback counters.
`functions` counts calls, not unique source definitions: retrying code generation
or scheduling may call QUALITY again. `prepassed_functions`, `rows` and
`global_values` cover successful prepasses. Do not divide unrelated sums and call
them an average function; retain profile/case/mode/replay identity.

## Population and density

`local_values` counts vregs with a real prepass touch interval; `global_values`
counts the function's ID universe. `local_values_{zero,small,tiled,large}_functions`
partition those successful-prepass functions into 0, 1–64, 65–512 and >512 values.
These are function-local populations, not distinct values in every basic block.

`raw_loop_spans` and `merged_regions` distinguish backward spans from the disjoint
closure regions actually consumed. `closure_value_tests` counts visited vreg/region
pairs, including invalid/nonintersecting intervals; `closure_intersections` counts
pairs that pass the intersection gate. `region_values_{zero,small,tiled,large}_regions`
partition merged regions by their intersecting-value population using the same
boundaries. A value spanning two regions counts in both, not once globally.

`candidates` is the retained heap population, **not all qualifying values**.
`candidate_cap_functions` records reaching the current 4096 cap; it does not
estimate omitted benefit. `empty_candidate_functions` records the existing empty
heap/empty-instruction early return. This does not repair #313's prefix policy.

`candidate_region_cells` is the allocated dense table population;
`candidate_region_updates` counts weighted spill/reload additions to that table;
`candidate_region_nonzero_cells` counts its final nonzero u32 cells. Density bins
`region_table_{zero,sparse,mixed,dense}_functions` partition constructed tables into
zero, (0, 1/8], (1/8, 1/2], and (1/2, 1] occupancy. Integer thresholds are floor
`cells/8` and `cells/2`, with no multiplication overflow. A diagnostic-only extra
walk observes final occupancy. The u32 arithmetic remains precisely current
#298 behavior: a wrapped-to-zero cell is zero here. Nonzero cells are **not** a
mathematically unbounded benefit count or an exact first-touch count.

## Clears, copies and lazy work

`candidate_region_clear_bytes` is table zeroing (4 bytes/cell).
`initial_value_clear_bytes` covers baseline traffic, raw traffic and the candidate
inverse-map initialization (three u32 arrays). It excludes the first durable pin
array initialization, prepass allocations and other allocator/arena clearing.

`heap_snapshot_bytes` counts the initial retained-heap copy;
`heap_restore_bytes` counts all actual attempt restores. `attempt_reset_bytes`
counts stores to the pin IDs, copied span endpoints, assigned/excluded counts and
pin depths for each entered attempt. It is reset/store traffic, **not exclusively
zero-fill**. `pin_mask_clear_bytes` counts the two per-instruction mask clears
before a pinned placement. These are logical bytes explicitly written by named
loops, not measured cache/memory transactions, retained bytes or RSS. The existing
allocation observer separately reports allocation requests and arena zeroing.

`prefix_rows_built` and `prefix_cells_written` count actual lazy foreclosure-prefix
construction, once per reached physical register per QUALITY invocation. They do
not claim the whole reserved prefix array was built or cleared.

## Probes and rejection work

`attempts` and `candidate_pops` count entered repacks and their heap consumption.
`marginal_rejections` preserves the current whole-interval caller-saved rejection
from #312, including its bypass of splitting. `whole_budget_rows` and
`split_budget_rows` count actual row checks, including early termination;
`whole_budget_rejections` and `split_budget_rejections` count failed checks.

`whole_register_probes` and `split_register_probes` count registers reaching the
prefix comparison, after capacity gates. Their `*_foreclosure_rejections` and
`*_overlap_rejections` count those failed gates. `whole_assignments` and
`split_assignments` count tentative assignments across all attempts, not just
pins retained in the final result. `split_cost_rejections` counts failed modeled
split comparisons; costs and tie rules are unchanged (#311).

`split_candidates` counts candidates reaching regional search.
`region_selection_passes` and `region_selection_cells` count complete dense-row
selection scans, **including the final unsuccessful scan**. `selected_regions`
counts selected nonzero cells, before legality/budget/cost checks;
`region_legality_rejections` combines entry/coverage and required-exit legality
failures. No next-region ordering is precomputed or changed.

`placement_probes` counts pinned FAST placement trials; the baseline FAST pass is
excluded. `placement_cost_rejections` includes invalid trial placements and
unprofitable trials. `pin_verification_rejections` counts failed final pin checks;
`accepted_placements` counts successful returns of a pinned placement. Other
bounds can terminate probes, so rejection counters are not a complete partition
of every popped candidate.

## Acceptance and next experiments

The existing split fixture checks one-call counter identities, histogram
partitions, dense-cell/clear and repeated-scan relations, heap snapshot/restore
accounting, and identical counter deltas and ordered placement output after dirty
scratch reuse. Counter boundary controls include zero and UINT64_MAX overflow;
the no-target early return must not fabricate prepass/table work. Enabled builds
must run these controls as well as ordinary full and sanitizer tests.

Before changing representation, collect small, dense and large adversarial cases
as well as representative frozen source. Report construction, allocator and
whole-compiler latency from appropriate uninstrumented measurements, not from this
observer. Exact output is required for representation-only experiments. The
historical endpoint-search closure prototype included a one-region regression;
#125/#128 retain that negative evidence. Hardware, phase latency and unavailable
counters must remain unavailable. These counts establish neither a Zen 5 speedup
nor a reason to force AVX-512 into a sparse or tiny population.
