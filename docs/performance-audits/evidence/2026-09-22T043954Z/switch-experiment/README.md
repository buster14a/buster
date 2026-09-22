# Switch-interval proof experiment

Research source pin: Buster `ec1897b900c694e57a10ee5ddaacd89c4cd7c57c`, tree
`1e20ddfb534ba16a05a277136ff4f391a93dc28f`. This isolated directory changes no
repository code. It contains executable equivalence tests and an opt-in timing
harness. **No kernel or compiler performance measurements were made.**

The useful first production experiment is the **online hull proof**, because
it can remove prior-pair probes without allocation, sorting, SIMD staging, or
changing diagnostic order. The sorted certificate and vector scans are strong
competitors to measure, not claimed improvements. Familiar interval-bounding
and sorted-separation mechanisms are applied here; no worldwide novelty claim.

## Source computation and scope

Pinned source:

- [Parser verifier and insertion](https://github.com/buster14a/buster/blob/ec1897b900c694e57a10ee5ddaacd89c4cd7c57c/src/buster/lib/compiler/frontend/c/c_parse.c#L20238):
  truncate endpoints to controlling width, validate order, compare inclusive
  interval against prior intervals, report overlap, append even after overlap.
- [Lowering overlap validation](https://github.com/buster14a/buster/blob/ec1897b900c694e57a10ee5ddaacd89c4cd7c57c/src/buster/lib/compiler/frontend/c/c_gen.c#L36927):
  repeats the pairwise question after typed case conversion; skips defaults,
  returns on earliest offending source case.
- [Diagnostic tie rule](https://github.com/buster14a/buster/blob/ec1897b900c694e57a10ee5ddaacd89c4cd7c57c/src/buster/lib/compiler/frontend/c/c_parse.c#L18221):
  the earliest token wins; equal-token checks retain the first diagnostic.

The isolated API returns the earliest source-order current index overlapping
any prior index for **already valid intervals**. Its normalization stage rejects
the first reversed interval with status 2. This separate invalid-input contract
does not reproduce all parser diagnostic semantics. In production, malformed
labels must stay excluded and the existing diagnostic loop must continue.

`pipeline` charges raw endpoint reads, normalization, allocation of two `u64`
arrays, the chosen computation, optional scratch allocation/sort/copy/scan,
result materialization and frees. Raw input generation is outside timing.
This is an endpoint pipeline, not an entire Buster compile. It excludes token
walking, constant evaluation, target-type resolution and extraction from the
larger `CIrSwitchCase` layout. Those costs must be included in integration.

The normalized scalar reference is deliberately strong: it normalizes each
endpoint once. It is not a cycle-faithful implementation of current parser
comparisons that XOR raw endpoints repeatedly, nor of lowering's larger AoS
records. A production experiment needs an incumbent → normalize-only → hull
ablation before attributing any effect to SIMD.

## Implemented competitors

| Variant | Work and storage after normalization | Main purpose |
|---|---|---|
| `scalar` | Source-order triangular scan, O(n²) | Exact order reference; compiler vectorization disabled |
| `hull` | Running minimum low/maximum high; scan only when interval enters hull | O(n) ascending, descending or outward cases; no extra storage |
| `certificate` | Hull-only proof; on unknown, iterative mergesort of private pairs; adjacent proof; exact original fallback on overlap | O(n log n) disjoint arbitrary order; all construction charged |
| `avx2` | Four prior `u64` lanes, unsigned comparisons via sign bias, scalar tail | Same quadratic algorithm, VEX width baseline |
| `evex256` | Four prior lanes, unsigned compare masks, scalar tail | Same width with EVEX predicates |
| `avx512` | Eight prior lanes, unsigned compare masks, scalar tail | Identical comparison algorithm at 512 bits |
| `avx512_u2/u4/u8` | Fixed 2/4/8 independent chunks per inner group | Limited scheduling ablation, not additional candidate algorithms |

`hull` checks `new_high < min_low || new_low > max_high`. If true, every
previous interval lies entirely to one side of the new interval. Otherwise
the old exact scan runs: being inside a hull is **not** proof of collision,
because there may be holes. Extrema update costs are paid on every record.

The sorted Boolean certificate uses `sorted[i-1].high < sorted[i].low` for
every adjacent pair. For `j > i`, `low[i+1] <= low[j]`; therefore an overlap
between interval i and any later j implies overlap with immediate successor
i+1. No prefix-max recurrence is needed. Sorting may change which overlapping
pair is observed, so a failed certificate replays untouched source arrays.
Stable mergesort has no callbacks or input-dependent recursion. The experiment
uses no unmeasured size threshold: it always charges sorting after an unknown
hull proof, which can lose badly on small or erroneous inputs.

## Correctness and actual evidence

`python3 build.py` ran GCC 13.3.0 optimized and ASan+UBSan builds. Both passed
**17,440 cases**, exercising all nine available variants on a shared AMD EPYC
9V74 guest. This is not Zen 5 evidence. The observed XCR0 value was `0xe7`.
Raw test results, exact build commands, binary hashes and source identities are
in `correctness.json` and `build_manifest.json`.

The suite covers empty/singleton/small lengths, every collision pair position
through n=33, every prior position at n=130/156/167/256/259, all 256 possible
eight-lane overlap predicates, 8/16/32/64-bit signed and unsigned normalization,
raw truncation bits, full-width extrema, equal endpoints, reversed intervals,
interior holes, nested intervals, random inputs, reverse/outward/permuted order,
aliasing read-only raw endpoints, and exact page-end arrays at n=0..97.

The first sanitizer invocation failed at **LeakSanitizer initialization under
ptrace**, not at a reported memory access. ASan+UBSan subsequently passed with
`ASAN_OPTIONS=detect_leaks=0`. Leak checking was not validated. Guard-page tests
are Linux-specific. No production compiler fixture, fixed point, output-file
equivalence, GitHub CI gate or native-retirement acceptance was run here.

Full vector loads occur only when the remaining valid prior count is at least
the vector width; tails are scalar. No masked loads or speculative invalid
pointers are required. Equality means overlap; no endpoint increment risks
overflow. Width-mask construction avoids shifts by 64. Intrinsic broadcasts
use GCC's defined unsigned-to-signed conversion modulo 2^64 on the selected
x86 compiler; portable scalar arithmetic stays `uint64_t`.

Runtime dispatch explicitly checks CPUID leaf 1 XSAVE/OSXSAVE/AVX, XGETBV state
bits, then leaf 7 AVX2, AVX512F and VL as needed. Unavailable variants are not
called. Non-x86 builds retain the scalar cores; the Python Linux timing harness
is not a cross-platform production dispatcher.

## Assembly result: source predicates did not stay independent

`assembly.txt` is the optimized binary disassembly. All builds disable loop
and SLP autovectorization. `overlap_scalar` uses scalar comparisons/branches;
`overlap_hull` uses scalar extrema/comparisons. The sort moves 16-byte records
with XMM copies, which does not vectorize its comparison algorithm.

GCC 13.3 turns the two source mask compares plus AND into:

```asm
vmovdqu64 zmm2, [previous_low]
vpcmpleuq k1, zmm2, zmm1
vpcmpleuq k0{k1}, zmm0, [previous_high]
kmovw ecx, k0
test cl, cl
```

The second comparison therefore **depends on the first mask**, and each tile
crosses to a scalar register. EVEX256 has the same structure. U2/U4/U8 perform
one mask-to-GPR move per tile and OR in scalar registers. U8 uses ZMM0–7 and
all mask registers, with a `KMOVW k1,k0` move because k0 cannot supply a
writemask, plus saved scalar registers. No ZMM/YMM spills were found. Do not
claim a retained mask accumulator, independent compare predicates, or an
eight-chain resource win from the source alone. The emitted mask moves are
`KMOVW` (AVX512F), not `KMOVB` (would add DQ).

The independent hardware appendix records original 9700X uops.info data:
ZMM VPCMPUQ has 6-cycle vector/mask dependency latency and reciprocal throughput
0.5 cycles; EVEX YMM has 4-cycle latency and the same instruction throughput.
The source-fused ZMM two-compare dependency is consequently modeled at roughly
12 cycles before scalar extraction, versus roughly 7 for two independent
compares plus KANDW. These are **instruction dependency models**, not timings
of this binary. A future measured experiment can test an independent-mask
lowering, but one more hand-tuned variant was deliberately deferred.

## Cost, break-even and likely failure modes

For a valid switch let `P=n(n-1)/2`, `P_f` the pair probes left after the hull,
`a` the effective baseline pair cost, `h` the proof/update cost, and `d` the
setup difference. A useful empirical break-even condition is
`a*(P-P_f) > h*(n-1)+d`. This is not a sum of independent hardware latencies:
loads, compares, branches and normalization overlap. Resource lower bounds
must use the maximum of load demand, eligible compare throughput, front-end
demand, and dependent-chain depth.

The original short-circuit scalar path logically reads roughly 8–16 bytes per
prior pair; the vector scan reads both endpoint streams, 16 bytes per pair,
plus current endpoints/broadcasts. Both normally reread small L1-resident data,
so these are not DRAM-byte claims. The new hull needs two live scalar words
in a producer-integrated design and can eliminate all prior-array reads on
successful certificates. It can pay overhead without benefit on interior gaps.

This reproducer adds normalized storage of 16n bytes to all variants, with
16n raw bytes read and 16n normalized bytes written. The sort adds peak 32n
scratch bytes, an initial 16n read/16n write copy, about 32n bytes per merge
level, and a final adjacent scan. Thus modeled scratch is 16n versus 48n total
for normalized arrays plus sort scratch, excluding input pool/libc metadata.
All is freed per call. A production parser can reuse its existing two arrays
and therefore has a different allocation cost. `process_peak_rss_kib` measures
the whole Linux process including input pool, not arena live-byte cost.

Static source census from the companion investigation found production `.c`
switches mostly small (library median 10, p90 27, p99 78, maximum 167), with
unresolved enum/macro value order. It is not an executed/preprocessed census.
No exclusive phase fraction `f` or measured local speedup `s` exists. Do not
claim an end-to-end percentage; the optimistic Amdahl expression remains
`1 / ((1-f) + f/s)` before overhead, with both variables unknown.

Abandon the first slice if current corpus pair cost is immaterial, hull success
is rare, small-call cost exceeds removed work, output/diagnostics differ, or
fresh paired compiler results stay inside the A/A noise floor. Large synthetic
switches establish scaling only; they cannot validate ordinary compiler gains.

## Reproduce checks and prepare measurements

```sh
python3 build.py
python3 -m py_compile run_pairs.py build.py generate_unroll.py
python3 run_pairs.py --dry-run --cpu 0 --out plan-only
```

The dry run creates metadata and the complete randomized schedule, with zero
timed runs. It does not reserve the host. The actual script rejects a non-9700X
unless explicitly labeled `--diagnostic-non-9700x`; this switch does not make
another CPU's result applicable to Zen 5. Do not run on the interactive desktop.

Use the existing authorized operator path to a quiet dedicated measurement
host. This harness is **not an admitted Buster service recipe** and must not be
sent to a fixed-recipe gateway as though it were one. Establish host access and
serialization under the current repository procedures. The local file lock
only prevents another copy of this harness from running concurrently; it does
not reserve all host activity or the SMT sibling. No machine-wide policy is
changed. Fill `context.example.json` with observed configuration and pass it.

First bounded measurement, emphasizing real size scale and the best scalar:

```sh
python3 run_pairs.py --bench --cpu 2 --out first-run \
  --context host-context.json --variants hull,certificate,avx2,evex256,avx512 \
  --lengths 4,8,10,16,27,48,78,130,156,167 \
  --patterns ordered,permuted --calls 20000 --pool 64 \
  --aa-repetitions 9 --repetitions 21 --seed 20260922
```

Choose an actually available, authorized core; CPU 2 is an example, not a known
benchpress topology choice. Preserve all CSV/JSON samples. Exclusions are
predeclared as **none**; errors or input/output mismatches abort with retained
raw output. Each pair shares source-generated inputs/seed/call count; pair
order is randomized, A/A precedes A/B per workload, and input sets vary across
independent pairs. The output includes median ratios, IQR and an exploratory
95% paired-bootstrap interval; these do not account for selecting winners from
many variants. A/A uses one binary and cannot establish build-root sensitivity.

Confirm a surviving variant with a fresh seed and held-out patterns before
integration. If width is promising, separately compare `avx512` against
`avx512_u2,avx512_u4,avx512_u8` only near the observed break-even. Use `reverse`,
`outward`, `ranges`, `overlap_early` and `overlap_late` as explanatory held-out
conditions. `--pool-mib 64` enlarges raw inputs; it does not prove cold-cache or
DRAM behavior. The supplied default is a sustained sequence of short endpoint
calls with a warmup, not a compiler-like thermal/power duty-cycle experiment.

The next *compiler* measurement must obtain counts of case length, pair probes,
signedness/order, hull successes and exclusive time on frozen real inputs,
then use uninstrumented matched trusted compiler builds through the repository
throughput procedures. Run normal output/diagnostic equivalence and fixed-point
gates for that integration. Record actual cycles separately from wall time;
the harness only uses wall-clock nanoseconds. PMU/IBS and intermittent-burst
experiments are not implemented or claimed here; use correctly mapped events
from the hardware appendix in separate controlled probes.
