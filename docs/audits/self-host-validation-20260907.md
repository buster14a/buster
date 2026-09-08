# Self-host validation, 2026-09-07

This is a local validation record, not a claim that a particular GitHub check
has passed. The invariant and evidence format are specified in
[the bootstrap audit documentation](../self-host-audit.md). CI must validate
the actual submitted head; results from another source revision do not count.

## Published integration

Source: `20cea1e9af744739ef342a5957a6ddab4c48995a`, integrated with main
`88ecc49c1a5c5fef614c388c730babe7686a689a`.

Clang 17, Linux x86-64, Release split build, debug information disabled for the
host build, two build workers and one external-test worker. The local container
has a 4-GiB memory limit; this does not replace the production unity CI build.
Node 22 ran the existing Wasm64 tests with `--experimental-wasm-memory64`.
No expected failures or tests were removed to obtain these results.

| Gate | Observed result |
| --- | --- |
| Native checker | 341 assertions, zero failures |
| Release compiler tests | 396,517 assertions, 40/40 modules, zero failures |
| Target/allocator matrix | 24/24 legs: 4 native execution and 20 disassembly-oracle checks; zero expected failures |
| Ordinary bootstrap | Stage 1 and stage 2 identical; existing alternate-backend gates passed |
| Repeated bootstrap | Three generated generations, two executions per producer; all phase, status, binary, metric and behavioral checks passed |

All six generated compilers were 39,168,448 bytes. Their SHA-256 was
`29fd558bce24abe5fcfcea58a25d3f26111452ab4893799b5c473d2d3aa2baea`.
The common phase-artifact hashes were:

- Tokens: `51bdad451c0992971117f0a81fd7b51979efafcc7b4e300ec9f4f8ec4b544494`.
- Canonical IR: `fc401845343a7d425c64d068a4d4744f81378122bfb544b01bbc9914f92ce4fb`.
- Selected MIR: `905783a91076dfcd5fea8bcee42d7f288d78928a6d110ee8f0e9ffc3564727a0`.

Hashes identify the retained artifacts, not permanent golden outputs. Source
changes, SDK changes and intentionally different build configurations may
change them; the gate compares contemporaneous independent executions.

## Regression sensitivity and alternate seeds

On the earlier local integration `511bc564a2b5351bbf00d347bd6e700f3317de66`,
based on main `ebfdd11705bc470564e726857e498f1c82aa746c`:

- Restoring only the old ordinary-comparator implementation made the new
  checker fail: it reported deterministic eight-byte outputs despite missing
  required metrics. The fixed implementation passed all 341 assertions.
- Restoring only the old machine CFG construction produced 28 machine-test
  failures. The fixed source passed all 395,653 assertions in that revision.
- A minimized ternary assignment through a struct member, with
  `-fno-canonical-local-promotion -fbootstrap-trace=<fresh-prefix>`, failed
  selected-MIR validation in the old implementation: error 10, block 1,
  instruction 14, operand 0. The fixed compiler compiled and executed it.
  Canonical promotion can conceal this particular fixture's missing edge;
  the selector fix is required independently of that optimization.

Four starting producers were exercised on that earlier source: the Release
host compiler, an ASan/UBSan Debug host compiler, a MIR_STACK-built compiler,
and a NONE-built compiler. Each ran the same three-generation, two-repeat
native audit. All 24 generated executables and their token/IR/MIR artifacts
were identical across those four runs. Each executable was 39,150,680 bytes,
SHA-256 `d8f306298f72fb2bb4c17fd20a9eeb7ff9c024a4a2e449e849e2b040c1e2f61f`.

The alternate-seed experiment changed only the native audit's initial compiler
path. Generated full compilers still used the normal default allocator; this
is not four complete allocator-specific bootstrap fixed points. The sanitizer
instrumented the host producer, not its generated children. The native checker
and host-seeded bootstrap audit were sanitizer-clean. This does not claim that
the entire Debug suite was sanitizer-clean: the separate dirty-arena-watermark
problem tracked in #235/#247 was not suppressed or incorporated into this PR.

## Reproduction

Use the normal repository bootstrap and configure the tree first. Each command
below is a separate gate; retain its exit status and evidence directory.

```sh
./build.sh self_host_audit_self_test
./build.sh test_self_host --config Release
./build.sh test_self_host_audit --config Release
./build.sh test_mode_matrix --config Release
./build.sh build --config Release -t test_all
```

A missing local TCC was handled with the repository's documented hosted Clang
bootstrap exception. TCC, non-Linux native execution and the complete host
compiler/configuration matrix were not validated locally and remain CI work.
The native audit retains exact per-child commands and all comparison inputs;
its successful exit alone is not a substitute for checking the retained `PASS`
marker, complete phase records and the actual CI run for the submitted commit.
