# Hosted host-path coverage map

This map describes what the hosted correctness contract can establish about
the **compiler's host implementation**. The authoritative commands are in
[`correctness.tests`](../tools/ci/correctness.tests); its headings and the
[command guides](commands/README.md) describe their individual fixtures. A
passing row means its shards completed that inventory, subject to the
exceptions below. It is not a function-level coverage report. The selection,
draws and qualification rules are in [CI](ci.md#the-vector-row).

## Rows and evidence

| Run | Vector host | Other coverage |
| --- | --- | --- |
| Code-changing pull request | `vector`, plain | No sanitizer rows or libFuzzer campaigns |
| Merge-queue candidate and full daily/dispatched runs | `vector`, plain, ASan/UBSan, TSan and MSan | `native` plain; frontend and ELF libFuzzer campaigns and a crash/replay proof |
| Plain-scope nightly runs | No guaranteed vector row | `native` plain |

The `x86-64` rows, which built the scalar host's compiler, are gone (#2924, the
owner's decision on 2026-11-17), and so is the scalar host: `host_isa.h` refuses a
target without every AVX-512 feature it names.

The table describes runs that execute hosted tests; a nightly dispatch may
reuse qualifying earlier plain-row evidence. The selector may exempt a pull
request with no relevant code changes. A failed, cancelled, skipped or
incomplete row is not a passing execution. Each `vector` compiler is built for x86-64-v4 plus the
additional features in [`host_isa.h`](../src/buster/compiler/host_isa.h), which
must take them. A candidate's CPU and OS must pass the
feature probe before it receives an item; no vector emulation is used. The
four vector shards of a configuration share one verified compiler product.
The `native` row draws qualifying CPUs too (#2924) and each of its items builds
its own compiler there, so it is vector evidence of `-march=native` builds on
whichever qualifying CPU models the draw found.

Each drawn item, native or vector, records its target and predicate, then runs `benchmark-lexer`
over `x86_64_encoder.c`; the entire input must use vector lexer kernels and
match the scalar twin. This is direct execution evidence for that lexer input,
not a counter proving that every vector implementation ran. The verdict
requires a passing qualified execution of every item. The plain vector
builder also runs the [seeded failure fixture](../tests/ci/vector-seeds.sh).
It compiles the same lexer comparison with two test-only changes to masked
loads. One changes a plus token to minus. The other drops the 65th byte.
Each input passes with the seed disabled. With the seed enabled, the
comparison reports a mismatch and the draw verdict fails the assigned item.

The full suites use the same inventory, sharded by row-specific costs: four
items a configuration.
The following are useful entry points into that inventory, not an exhaustive
claim about every kernel or input:

| Host implementation area | Inventory evidence | Boundary or oracle covered |
| --- | --- | --- |
| Lexer (`c_lexer.c`) | `test-lexer` and language-option variants | Scalar comparison and protected-page inputs; see [lexer](commands/lexer.md) for the aligned-load limitation. |
| Preprocessor and parser (`c_preprocessor*.c`, `c_parser_impl.c`) | `test-preprocessor`, `test-parser`, repository and frozen corpus comparisons | Scalar twins, boundary fixtures and Clang comparisons; see [preprocessor](commands/preprocessor.md) and [parser](commands/parser.md). |
| Type layout and lookup (`c_types.c`) | `test-types` | Scalar twins, record-layout oracle, vector lengths and protected-page conversion inputs; see [types](commands/types.md). |
| Encoder, assembler and branch sizing (`x86_64_encoder.c`, `assembler*.c`, `branch_sizing.c`) | `test-x86-encoder`, `test-assembler --cross-check`, `test-branch-sizing --cross-check` | Scalar comparisons, protected boundaries, and GNU assembler cross-checks where specified; see [encoder](commands/x86-encoder.md), [assembler](commands/assembler.md), and [branch sizing](commands/branch-sizing.md). |
| Dataflow (`dataflow.c`) | `test-dataflow` | Scalar/vector fixpoints, independent reachability checks, boundary sizes and protected pages; see [dataflow](commands/dataflow.md). |
| Checker, lowering, IR, allocation and emission | `test-checker` and corpus tests, `test-lowering`, `test-ir-*`, `test-tier1-*`, `test-emission-metadata` | Unit and end-to-end fixtures, independent oracles and scalar comparisons where those tests specify them. Consult each [command guide](commands/README.md); execution of every host kernel is not separately attested. |

There is no scalar host build to fall back to: a host without the predicate
can't build the compiler. Conversely, `-march=znver5`, `--simd`, and vector
widths in a fixture can describe the **generated program**, not the host
compiler. Those options alone do not establish host-vector execution.

## Limits and unqualified areas

- A vector item attests the lexer execution above and completion of its suite;
  it does not measure per-function, feature-subset, branch, lane-mask or
  instruction coverage. The other production vector kernels, including cases
  absent from the table, have no separate runtime execution attestation here.
  A passing row therefore does not qualify every vector implementation or
  every tail, alignment and fallback boundary.
- Pull requests have no sanitizer rows or hosted libFuzzer campaigns in their
  own run; the inventory does include preprocessor fuzz tests. The merge-queue
  candidate runs the full contract on its own revision. The full-scope frontend
  libFuzzer campaign, the ELF override campaign and the crash/replay proof use
  `native` on a drawn AVX-512 runner (`drawn.yml`'s `fuzz` row, #2924), so each
  runs the vector host path compiled for its runner's CPU (the last two used
  `x86-64` before #2924); none is a vector-target (`x86-64-v4`) campaign.
- ASan combines address, undefined-behavior, bounds, leak and stack-lifetime
  checks; standalone UBSan is manual. TSan and MSan omit the whole-compiler
  `main.c` checking comparison, although separate `-c` compiles and their
  object comparison run on an otherwise idle shard. Generated machine code and
  external tools are not instrumented by the host build. Some FS-switched
  fixture code is deliberately uninstrumented, and sanitizer resource-failure
  tests substitute injected failures for some OS-limit tests. See the detailed
  [sanitizer policy](ci.md#sanitizer-builds-and-detection-policy).
- The vector target uses generic x86-64-v4 tuning, not the measured `znver5`
  binary; hosted virtual machines also have different CPU, OS and page-policy
  behavior. Each nightly snapshot runs the full native suite. A backlog state
  with qualifying full hosted evidence runs the [native smoke](../tools/ci/znver5-smoke.tests)
  and its selected measurements. Other backlog states run the full native suite. Neither the hosted row nor this map establishes
  native performance or complete Zen 5 equivalence.
- The full SQLite Tcl suite is nightly, outside the ordinary hosted inventory.
  Self-host qualification is a separate workflow, with a bounded smoke and a
  conditional native path. It is not part of every hosted row; the queue
  requires it as an auxiliary workflow when the changed paths select it.

When the workflow, host predicate or inventory changes, update this map along
with the detailed CI and command guides. A new test or a selected host target
alone does not prove that all its intended vector branches executed.
