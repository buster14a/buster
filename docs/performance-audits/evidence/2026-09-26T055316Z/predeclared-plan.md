# Predeclared plan: preparation locality interaction (frozen before variant measurement)

Base: main ade6ac4b6ecb21f30b61b656439bac476c145e2f (tree 4c5306221fdb22fccc929b55e333163742de17d0).
Variants: base; A = 343ed8df4ff37065e22e1b8317a2d63ef372440e (validation pass visited a function at a time);
B = d3a8d872cb1016d6d554b18fbdd5e6bab655046b (ablation only: FAST+publication fused only where no
output check intervenes, no per-function validator); A+B = 207ea447810d89b1b71e24263a1787bda2e8c670.
Configurations: checked (BUSTER_INCLUDE_TESTS=1, so BUSTER_IR_TRANSFORM_CHECKS=1; the trusted Release
subject) and unchecked (BUSTER_INCLUDE_TESTS=0, production). All binaries built serially per variant from
one worktree path with one compile command (only -march and BUSTER_INCLUDE_TESTS vary by cell).

Resource / threshold: IR rows+values of the whole module vs last-level cache capacity (module-wide sweeps
refetch from memory when the module exceeds the LLC); per-function working set vs private cache.

Predicted cold module-wide row sweeps inside ir_prepare_canonical_module:
  checked:   base 7 (P, V.own, V.chk, F, V.own, V.chk, Pub)  A 5  B 7 (same code path)  A+B 2
  unchecked: base 5 (P, G.own, G.chk, F, Pub)               A 4  B 4                   A+B 2
Predicted interaction in sweeps saved: checked +3, unchecked +1 (super-additive).
What each single change should fail to achieve: A alone leaves each validation as one cold sweep in its
own module-wide loop; B alone cannot fuse across a whole-module validation, so in checked builds it does
nothing and in unchecked builds only FAST+publication fuse. Only A+B places validation inside the loop
that just touched the rows.

Mechanism metric (deterministic, callgrind 3.22 cache simulation, x86-64-v3 builds, D1 48 KiB 12-way,
I1 32 KiB 8-way, LL 32 MiB 16-way = "exceeds" side): simulated LL data read misses (DLmr) inclusive of
ir_prepare_canonical_module, whole-run DLmr and DLmw, and Ir. Control ("fits" side): LL 256 MiB.
Interaction statistic: I = M(A+B) - M(A) - M(B) + M(base) for M = prepare DLmr and whole-run DLmr.

Workloads (all compiled from frozen inputs; same flags per workload across variants):
  W1 stage-1 unity self-compile of the frozen base tree, -g (primary); W2 same with -g0;
  W3 same with -fno-frontend-ssa (promotion-heavy);
  W4 synthetic harness corpus (bench_throughput generate, ci profile; smoke profile as small population;
     large_function at scales crossing the per-function private-cache threshold);
  W5 held-out real inputs available offline: repository tests/*.c corpus (small/adverse population),
     individual non-unity compiler TUs (real medium TUs).

Refutation criteria:
  R1 checked, LL 32 MiB: if prepare DLmr saved by A+B is not larger than (saved by A) + (saved by B)
     by at least 1.5 cold sweeps' worth, the complementarity claim is refuted.
  R2 checked: if B differs from base by more than 1% in prepare DLmr, "B alone is a no-op" is wrong.
  R3 if at LL 256 MiB the relative prepare DLmr reduction of A+B matches the 32 MiB reduction, the
     LLC-threshold explanation is refuted.
  R4 Ir across variants must agree within 0.5% of prepare Ir (pure locality, no work change); a larger
     difference must be explained before any interaction is claimed.
  R5 any output difference (object/executable bytes, diagnostics, exit status) for any workload/option
     set is a correctness failure and stops the experiment.

Timing: the approved exclusively leased 9700X route (9700x-service-dispatch.yml) is disabled and its
deployment (#880) is excluded from this task; no timing claim will be made. Local and hosted wall times
are not performance evidence. The ordinary PR compiler-throughput regression guard runs as usual.
Uncertainty: callgrind counts are deterministic for a fixed binary/input; repeated runs are compared for
exact equality rather than averaged. Simulated misses model neither prefetching nor memory-level
parallelism and are a mechanism diagnostic, not a latency estimate.
