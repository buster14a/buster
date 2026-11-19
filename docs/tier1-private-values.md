# Private local values in Tier 1

An immutable scalar parameter can keep the value that arrived at the ABI boundary. A mutable scalar can keep its latest value when every read follows a write in the same basic block and lifetime. Tier 1 ordinarily emits loads and stores for these locals and discovers their values again during promotion. This experiment removes that round trip at the handoff from the checked typed stream to Tier 1. It leaves frontend checked-expression work and host-kernel consolidation with their existing owners. [#2850](https://github.com/buster14a/buster/issues/2850) · [#2924](https://github.com/buster14a/buster/issues/2924)

Build the experiment with Buster's normal Release build command and one additional define.

```sh
BUSTER_BUILD_DEFINES=-DBUSTER_TIER1_PRIVATE_VALUES=1 ./build.sh --config Release
build/compiler --tier1 -c example.c
```

The standard build uses the established constructor. The define selects the private-value implementation at compile time. For experimental timings, pass the same define to the parent revision so the build system uses the same unit partition in both arms.

## Handoff and checks

1. The lowering proves the local is private before the counting and emitting passes. Taking its address, an implicit compare-exchange destination, incompatible access classes or observable storage disqualifies the local. Lifetime starts clear the local's preceding-write proof. Source-origin lowering, hidden results, variadic entries and asm bodies use storage form. Ordinary shared IR inspection also requests storage form.
2. Full GVN and SROA can observe storage identity before promotion. Both the ordinary body and its inline summary use storage in these configurations. When restricted vector passes are enabled, the inline stage publishes whether the unit contains a lowered vector body. A scalar wrapper can acquire such a body through more than one inline call, so this decision covers the entire unit.
3. The promotion adapter groups reachable access instructions by local. A read with an earlier write in its block resolves immediately. An exposed read follows predecessor links and caches the result. A joining block receives a provisional value before its operands are examined, which handles backedges. The shared simplifier resolves aliases across locals. The existing emitter publishes canonical phi rows with the original predecessor order.
4. Escape classification already decodes input operands. It saves those whose definition and effective use are in different blocks, including a phi's incoming predecessor. The dominance check visits these records after computing dominators. Same-block ordering and output verification still use the existing IR checks.

## Evidence and remaining work

On a Ryzen 9 7940HS, twelve paired `--tier1 -c` runs on one core reduced median wall time by 2.62% for SQLite and 1.44% for Lua. musl took 0.74% longer. The six-core wall-time changes were −0.34%, −0.51% and −0.84%, with eight of twelve pairs faster in each case. The 1,371 objects were byte-identical in each of six configurations, including full GVN, SROA, all passes and debug information. The shared lowering retained its frozen digest. The 9700X was unused. [Paired timings, counters, inputs and correctness evidence](https://github.com/buster14a/buster-performance/tree/98386c80e263326e7fad93ddf691b4d3e8fd0ffd/docs/measurements/tier1-private-values-zen4-pr3067-2026-11-19)

Demand can reach many provisional joins that the simplifier later removes. In the earlier SQLite trace it built 125,960 provisional joins and published 6,665. An independent replay of live iterated dominance frontiers proposed 7,222 locations and covered every published join. Across all three corpora it found every one of the 12,318 published locations, with 1,271 extra candidates. A native constructor must also supply the correct value on every incoming edge, handle cyclic aliases and bound its scratch use. Its effect on end-to-end compilation is an open measurement. [Replay and recorded graphs](https://github.com/buster14a/buster-performance/tree/98386c80e263326e7fad93ddf691b4d3e8fd0ffd/docs/measurements/tier1-private-values-zen4-pr3067-2026-11-19/graphs)
