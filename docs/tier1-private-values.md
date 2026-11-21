# Private local values in Tier 1

An immutable scalar parameter can keep the value that arrived at the ABI boundary. A mutable scalar can keep its latest value when every read follows a write in the same basic block and lifetime. Tier 1 ordinarily emits loads and stores for these locals and discovers their values again during promotion. The experiment changes the handoff from the checked typed stream to Tier 1. It starts after the frontend work on checked expressions and reuses the project's shared verification, simplification and emission. [#2850](https://github.com/buster14a/buster/issues/2850) · [#2924](https://github.com/buster14a/buster/issues/2924)

```sh
BUSTER_BUILD_DEFINES=-DBUSTER_TIER1_PRIVATE_VALUES=1 ./build.sh --config Release
build/compiler --tier1 -c example.c
```

The standard build uses the established constructor. The define selects the private-value implementation at compile time. To reproduce its measurements, pass the same define to the untouched parent so the build system uses the same unit partition in both arms.

## Handoff and placement

1. The lowering proves a local is private before the counting and emitting passes. Its address, an implicit compare-exchange destination, incompatible access classes or observable storage disqualify the local. Lifetime starts clear the preceding-write proof. Source-origin lowering, hidden results, variadic entries and asm bodies use storage form. Ordinary shared IR inspection also requests storage form.
2. Full GVN and SROA can observe storage identity before promotion. Both the ordinary body and its inline summary use storage in these configurations. When restricted vector passes are enabled, the inline stage publishes whether the unit contains a lowered vector body. A scalar wrapper can acquire one through several inline calls, so this decision covers the unit.
3. The promotion adapter groups reachable reads and writes by local. A block's leading reads seed backward liveness. A local with no exposed reads needs no joins. When it has a single defining block that strictly dominates every exposed read, that definition already supplies them all. The first local requiring general placement triggers construction of sparse dominance-frontier rows for the function.
4. The defining blocks seed a worklist. Each live frontier block receives one phi, and that phi becomes another definition. Renaming then follows immediate dominators to a concrete write or a placed phi, caching the block-entry result separately from the final write. Incoming operands follow the CFG's original predecessor order. The shared simplifier resolves cross-local aliases and the existing emitter canonicalizes the result. The constructor caps the sparse frontier target column at 16 MiB. On overflow or scratch exhaustion it restores the arena and uses the existing bounded constructor.
5. Escape classification already decodes input operands. It records those whose definition and effective use are in different blocks, including a phi's incoming predecessor. The dominance check visits these records after computing dominators. The ordinary IR verifier still checks same-block ordering and the result.

## Measured tradeoffs

On a local Ryzen 9 7940HS, twelve paired `--tier1 -c` runs reduced one-core wall time by 3.75% for SQLite and 4.38% for Lua, with the PR faster in every pair. One-core musl took 2.19% longer. On six cores the changes were −0.64%, −0.52% and −0.27%, respectively. The ordinary and all optional-pass object comparisons covered 1,371 inputs in each of six configurations. The dedicated two-entry cycle and transitive vector-inlining fixtures also ran through the strict native compiler.

On the same SQLite input, provisional joins fell from 179,634 to 7,222 and provisional incoming operands from 431,209 to 20,378. Both constructors published 6,665 joins. Largest per-promotion arena growth fell from 1,506,588 to 670,792 bytes. A generated 11,010-block function crossed the sparse frontier budget and ran correctly through the bounded constructor. Ordinary Release builds also produced identical objects for all three corpora with plain `-c`, with paired user-instruction changes below 0.001% in absolute value.

The earlier demand-driven version supplies a control for the placement decision. On one core the native frontier reduced SQLite wall time by a further 1.46% and Lua by 0.86% relative to that version. The frontier took 1.01% longer on musl. The Ryzen 7 9700X stayed unused. [Input manifests, raw timing pairs, counters, native placement counts and reproduction commands](https://github.com/buster14a/buster-performance/tree/13da01bc2726c7b4e5b3bd5219cc1210367cb749/docs/measurements/tier1-private-values-zen4-pr3067-2026-11-19/native)

The measured parent includes #3030 from #2850. The opt-in build also compiled cleanly with main after #3047 from #2924 and passed both IR suites and the strict fixtures on their combined tree. [Integration check](https://github.com/buster14a/buster-performance/blob/13da01bc2726c7b4e5b3bd5219cc1210367cb749/docs/measurements/tier1-private-values-zen4-pr3067-2026-11-19/native/checks/main-integration.txt)
