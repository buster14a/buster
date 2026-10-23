# Foreign section GC measurements, 2026-10-23

These are single-lane static-executable fixture measurements on the shared
Ryzen 7 9700X. They support the bounded optional section-GC adapter; they do
not qualify dynamic linking, a corpus, COMDAT performance, personality/LSDA
handling, or scaling. Issue #86 remains open.

## Source and validation

- Source commit: `4cb4936aaef2b980e4b2f29101cf8e86879f8336`.
- [Frozen source manifest](source.sha256), SHA-256:
  `ac2e9a90d41404b8d9c8cda79662c39cb80d1f8504a6170a5dcfb4dc48eee56b`.
- Compiler SHA-256:
  `dae6eedb7a04bba61c547eb49f41023523c2a0d945cc5e076d360d21c1b43bcc`.
- Benchmark source SHA-256:
  `56be066b8a58c926136d1b2d77f05d02b2ae9135fcc6734d300f7ee4445f0224`.
- Remote source directory:
  `/home/david/buster-fast-bench/codex-linker-foreign86-gc-v1`.

The [build](build.log), [full foreign cross-check](test-foreign.log), and
all three benchmark processes exited 0. The cross-check includes 36,694 GC
marker checks, 281 adapter checks, and 72 C++ unwind checks; every earlier
test group also passed. Each benchmark process reports 1,118 independent
pipeline checks with zero failures, in addition to executing and checking
the complete bytes of each sampled output outside the clock. The independent
oracle checks original ELF function identity and bytes, section/symbol/name
maps, removed FDEs, retained EH offsets, and serialized FDE addresses/ranges.
All-live outputs must be byte-identical across the four variants.

Each command below ran separately, prefixed by
`EXCLUSIVE_OWNER=codex-linker-foreign86`,
`EXCLUSIVE_TITLE` describing that run, and
`EXCLUSIVE_LINK=https://github.com/buster14a/buster/issues/86`, through
`/home/david/buster-fast-bench/exclusive.sh`. CI retained priority. Build and
cross-check commands were `./build.sh` and
`./build/compiler test-foreign --cross-check --keep` in the frozen directory.

```text
taskset -c 2 ./build/compiler benchmark-foreign-gc --counts 8,1024 --runs 21 --keep
taskset -c 2 ./build/compiler benchmark-foreign-gc --counts 8,1024 --runs 21 --order gc-vector,no-gc-vector,gc-exact,no-gc-exact --keep
taskset -c 2 ./build/compiler benchmark-foreign-gc --counts 8,1024 --runs 21 --schedule blocked --keep
```

The complete logs are [run 1](run1.log), [run 2](run2.log), and
[run 3](run3.log). The first two rotate variant order between samples; the
third blocks samples by variant. No scalar conditioning was requested.
Retained input directories are `/tmp/buster-foreign-gc-A45tMx`,
`/tmp/buster-foreign-gc-lu4Otn`, and `/tmp/buster-foreign-gc-rqA34Y` on the
measurement host. Each directory's sorted `sha256sum -- *.o` manifest has
SHA-256 `0ac95f2a6736377a878a69d33dcf4a99e08c9fe60303c78619e69e83fc4ee57f`.

## Interpretation

The [stage guide](../../object-writer-and-linker.md#foreign-section-garbage-collection)
reports the ranges of the three process medians. These ranges describe
observed spread; they are not confidence intervals. The large mixed-dead
fixture removes half the leaf functions and FDEs, shrinking the executable
from 91,040 to 50,080 bytes. Total measured link time falls beyond the
between-process median spread, while retained arena bytes increase.

The all-live control also gets faster despite removing nothing. The adapter
copies live bytes into a new arena, changing placement and memory access.
These results therefore do not establish a reachability-only or SIMD-only
speedup. Page-fault/cache attribution and peak RSS were not measured. The
eight-leaf results remain near the observed spread. GC remains optional.

Each sample includes fresh arenas and input maps, resolution, conversion,
optional GC, final writing/close, and cleanup. Fixture and command generation,
output unlink, conditioning, output execution, and validation are excluded.
Inputs and filesystem caches are warm. Retained arena bytes exclude mappings
and transient scratch; they are not peak process memory. Phase medians must
not be summed. TSC ticks are reference ticks, not core-cycle measurements.
