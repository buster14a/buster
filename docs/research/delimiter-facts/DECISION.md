# Decision: publish delimiter facts once

Baseline: `c5a073139691e9111986c4fca6f6b03d2a6dfcf1`.
Owner [#2183](https://github.com/buster14a/buster/issues/2183);
candidate [#2191](https://github.com/buster14a/buster/pull/2191).
The [original audit](../../performance-audits/2026-10-01T204938Z.md)
contains the anchored work account, overlapping ownership and verified licenses;
the [package](README.md) contains exact reproduction commands and raw evidence.

Select bidirectional delimiter publication as a bounded test of work elimination.
The token-position-index producer already knows both endpoints, but three
semantic consumers reconstruct the reverse relation. Publishing the reverse
answer in existing unused array positions removes that reconstruction without
another representation or allocation. Canonical IR and its validation boundaries
remain unchanged; compiler code stays portable C with no new dependency.

| Rank | Route | Decision |
|---|---|---|
| 1 | Producer-owned delimiter pairs | Implement this small, cheaply falsifiable slice; no new capacity or invalidation |
| 2 | Demand-driven x86 encoding cells, #1295/#2017 | Larger plausible prize for separate-process small inputs; fresh attribution and existing-owner coordination required |
| 3 | Broader semantic answer reuse, #1028/#1247 | Existing memo already captures many eligible repeats; type materialization and invalidation make this a larger correctness change |

The x86 route still enumerates 6,912 variable-memory cells / 11,520 metadata
encodes. Mandatory frame projections need only 256 cells / 512 encodes initially,
but consume eight tables, defeating simple whole-table laziness. Per-cell demand
adds readiness (up to 96 bytes/table), first-use branches and serial gang-prewarm
obligations. Its current affected fraction is unknown. Historical separate-process
Lua startup shares predate already-landed preparation removals. Existing batching
already demonstrated a larger workload-shape improvement; it is not a new unity
compiler improvement.

| Delimiter choice | Work retained / added | Ownership cost |
|---|---|---|
| Keep current | Repeated allocation, 4T clearing and T-position reconstruction per map | Proven independent consumer scratch |
| Simpler shared body map | Retains one reconstruction and clearing pass | Shared scratch lifetime/range rules and call-chain coupling |
| Producer publication | Adds one four-byte store per matched pair and checked reverse queries; removes all reverse builders | Existing parse-owned immutable index; no extra capacity/invalidation |

Invariants: exact forward answers, UINT32_MAX for non-openers, reverse clipping
with `start <= open < close`, unchanged malformed-delimiter recovery and arena
lifetime. Before implementation, #2183 and commit `8e78180790679855bfd0f0c01a6e99ff23084ea5`
declared baseline self-host, independent pair oracle, malformed/clipped/dirty
storage cases and output/diagnostic/mode/self-host falsification. These checks
pass in strict cloud run36923086652; its source candidate is `187b463a305ddd80ac26a6e855916b741e677823`.

Observed frozen-unity work: 5,705 maps, 2,042,050 visits and 8,168,200 logical clear
bytes become zero. Added reverse stores are 450,797 / 1,803,188 logical bytes.
No physical DRAM or live-memory claim follows from these counts.

Observed removed-region time is 0.1190% direct / 0.1110% promotion of instrumented
-g compilation. Conditional diagnostic-regime ceilings are approximately
1.00119x / 1.00111x before added work; they are not bounds for the plain -g0
compiler. A 2x gain requires net removal of half of complete compilation time.
This slice provides no credible step-change or 2x route.

Plain cloud unity medians have opposite signs across captures:
3.532426 -> 3.543692 s first, 2.347147 -> 2.317209 s strict. Strict updates/labels
regress 1.10% / 1.73%. Short shared-host samples do not establish a stable gain.
The identical small-input RSS floor is unresolved.

The [qualified-host audit](../../performance-audits/2026-10-07T204035Z.md)
records the 2026-10-06 Ryzen 7 9700X comparison at head `96c75ddb` against
merge base `12cde021`: tests-off Clang Release, same frozen source, 208 ABBA
pairs with byte-identical output. Wall B/A was 0.997967 with 95% CI
[0.997531, 0.998526], **below the 0.5% practical floor**. Peak RSS had no
detectable difference; executable-section bytes matched exactly. The older
comparison profile did not include a throughput corpus. Main was subsequently
merged into branch head `7199058a`, so this is not a new measurement of that
exact head or its current-main combination.

Decision: retain the smaller producer-owned implementation for its removal of
three independent reverse builders and unchanged arena ownership. Claim work
elimination and simpler source, **not a measurable whole-compiler speedup** or
a 2x route. Current-head correctness, deferred Apple/iOS lanes and combined
tree admission are separate integration checks. No external implementation is
imported; license component distinctions remain in the original audit.
