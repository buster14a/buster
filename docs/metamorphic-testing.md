# Source-equivalence metamorphic tests

`ide test` includes a deterministic, bounded native smoke campaign. `ide
metamorphic` exercises a wider target matrix. Both are implemented in C in
`src/buster/tests/compiler/metamorphic/metamorphic_test.c`; there is no script
harness, source-rewriting dependency, or new compiler executable.

## Run

Build normally, then run the native smoke tests through the existing test target:

```sh
./build.sh build --config Release -t test_all
```

Run the wider campaign (four seeds by default):

```sh
build/Release/ide metamorphic
```

A larger deterministic campaign, with a fresh artifact directory:

```sh
BUSTER_METAMORPHIC_SEED=100 BUSTER_METAMORPHIC_CASES=16 BUSTER_METAMORPHIC_OUTPUT=build/metamorphic-seed-100 build/Release/ide metamorphic
```

The default uses direct frontend SSA. Set `BUSTER_METAMORPHIC_FRONTEND_SSA=0`
to exercise the reference frontend explicitly with the same seeds and targets.
Both independent reference and Buster invocations preserve the repository's wrap,
aliasing and unsigned-plain-char options. This grammar itself uses unsigned
arithmetic and does not rely on signed overflow or aliasing exceptions.

Require actual execution rather than accept compile-only rows on one native target:

```sh
BUSTER_METAMORPHIC_TARGET=linux-x64 BUSTER_METAMORPHIC_REQUIRE_EXECUTION=1 build/Release/ide metamorphic
```

Drive a second compiler binary with the same generator, oracle and reducer:

```sh
BUSTER_METAMORPHIC_COMPILER=/absolute/path/to/baseline/ide BUSTER_METAMORPHIC_CASES=1 BUSTER_METAMORPHIC_TRANSFORMS=256 BUSTER_METAMORPHIC_OUTPUT=build/metamorphic-baseline build/Release/ide metamorphic
```

The executable needs `BUSTER_INCLUDE_TESTS=1`. Shell environment assignment above
is POSIX syntax; on Windows set the same environment variables before invoking
`build/Release/ide.exe metamorphic`. Run from the repository root. Choose a fresh
output directory: each pair and reducer replay owns a monotonically numbered
`work-N` directory, and an existing campaign folder is not an append-only database.

## Generated programs and semantic preconditions

The generator renders a small typed grammar, rather than modifying arbitrary C
with regular expressions. A seed chooses one to three arithmetic terms, one to
three loop iterations, and small constants. Eight fixed input pairs include zero,
equal and unequal inputs, the 32-bit boundary, the 64-bit sign boundary, and
`UINT64_MAX`. Computation uses `unsigned long long` on the supported 64-bit-integer
targets; a generated value-range assertion enforces that contract. Overflow is
unsigned modular arithmetic. Pure local helper calls are allowed; there are no external calls,
volatile accesses, unsequenced side effects, floating-point operations, aliasing,
uninitialized values, shifts or division.

| Mask | Transformation | Why it preserves this grammar |
| --- | --- | --- |
| 1 | Rename locals | Fresh names, one known scope, declarations and uses renamed together. |
| 2 | Reorder independent declarations | Both initializers read only distinct function parameters. |
| 4 | Add redundant parentheses | Wrap operands and the return expression, without changing precedence. |
| 8 | Split products through temporaries | Pure subexpressions, identical unsigned type, no evaluation-order effect. |
| 16 | Rewrite control flow | A bounded `for` becomes `while`; an equality branch is inverted and its arms exchanged. There is no `continue`. |
| 32 | Insert dead code | A well-typed `if (0)` assignment is never evaluated. |
| 64 | Swap commutative operands | Only pure unsigned addition and multiplication are swapped. |
| 128 | Reformat whitespace/comments | Replace spaces at generated token boundaries; leave preprocessing directive lines intact. The grammar contains no quoted literals. |
| 256 | Spell local names through empty-prefix token pasting | `META_NAME(,local)` must expand to the same identifier. This is the permanent issue #220 regression. |
| 512 | Materialize and copy an aggregate | Explicitly initialize a two-element unsigned array inside a struct, copy the struct, and read only its initialized elements, never padding. |
| 1024 | Outline products into a helper | A nonrecursive, pure, same-translation-unit call computes the same unsigned product. Argument evaluation order is unobservable. |

Each enabled transformation runs separately and, when more than one is enabled,
as one combined mask. The default is twelve pairs per seed and target/mode (eleven for eBPF). This is not an
arbitrary-source C transformer and makes no equivalence claim for unsafe
floating-point reassociation, side-effecting operand swaps or scope-changing
rewrites.

A separately written host evaluator computes full-width expected values. Each
generated native/LLVM/Wasm executable checks all eight values and returns the
first failing input index, or zero. This avoids truncating the computed value to
an eight-bit exit status. eBPF checks the same full-width values directly. The
wider campaign first compiles and executes each generated pair with available
Clang and GCC commands, each at `-O0` and `-O2`, using C11 and identical semantic
flags. A failed reference check stops the campaign rather than promoting an
invalid generator case as a Buster compiler bug. Missing reference commands are
reported explicitly, not counted as passes. Missing Clang also prevents LLVM
execution. Save both compiler versions: a command named `gcc` may actually be
Apple Clang, so its name alone does not establish an independent implementation.

The ordinary module also checks deterministic rendering for seeds 0, 1, 42 and
`UINT32_MAX`, every individual relation and their composition. Seeded valid and
invalid token-paste controls run both plain and reformatted, using only
`c_preprocess`; invalid sources are never linked or executed. These assert the
preprocessor diagnostic contract, not a runtime result for invalid C.

## Backend and allocator coverage

The table enumerates Buster's canonical-C output families on the pinned
implementation. Every row receives every value below
`CODEGEN_REGISTER_ALLOCATOR_MODE_COUNT`: currently `none`, `mir-stack`, `fast`
and `quality`. Native fallback behavior is preserved; requesting a mode is not a
claim that every function used that allocator. LLVM, Wasm and eBPF are separate
emitters and do not use the native register allocator even though all four
accepted option settings are exercised.

| Target row | Execution avenue |
| --- | --- |
| `linux-x64`, `linux-arm64` | Native when host-compatible; `qemu-aarch64` for foreign AArch64 when found. |
| `windows-x64`, `windows-arm64` | Native when host-compatible; `wine` for foreign x86-64 when found. |
| `macos-x64`, `macos-arm64` | Native when host-compatible. |
| `llvm-native` | Buster emits bitcode; host Clang consumes it and the resulting executable runs. |
| `wasm64` | Node's WebAssembly engine with Memory64 enabled. |
| `ebpf` | The existing bounded test-only eBPF interpreter, not the kernel verifier/JIT. |

Foreign native rows still compile and link when execution is unavailable. They
are reported as `unexecuted`; no disassembly check is called a behavior check.
A runner which is found but fails to launch/validate/execute is a failure, not a
skip. Before a Wasm campaign, Node validates a fixed Memory64 module without
extra options. If needed, the harness retries that capability probe with
`--experimental-wasm-memory64` and uses the successful invocation for generated
programs. If both probes fail, campaign setup fails explicitly before source
generation or reduction. This supports both engines where Memory64 is enabled
by default and older engines that require the flag.

The eBPF interpreter supports the generated subset and has bounded instruction
execution and checked stack accesses. It does not implement local calls: only
mask 1024 is excluded for that row, with an explicit
`METAMORPHIC_TRANSFORMS_UNAVAILABLE` record. All previously supported relations
and aggregate materialization still run. This is a coverage exclusion, not an
execution pass for the call relation. This change adds multiplication to that
existing interpreter, with separate 32- and 64-bit multiplication cases in its
existing compiler tests. An interpreter refusal is reported as a runner failure,
not incorrectly classified as a successful guest result or a proven compiler bug.

The local aggregate-copy relation is lowered by both nonnative backends.
Wasm64 uses private shadow-stack snapshots and bulk memory operations; eBPF
allocates each snapshot within its existing 512-byte frame and copies exact
bytes without over-reading packed objects. Neither representation aliases a
mutable source object. The canonical IR and aggregate function ABI contracts
are unchanged. Aggregate block parameters and bit-field aggregate construction
remain explicit unsupported cases; eBPF snapshot alignment is at most eight
bytes, and oversized frames still fail with a diagnostic.

The same five repository-relative C cases cover plain, packed, nested and union
copies plus independent mutations. The ordinary driver suite checks both
frontend forms through Node for Wasm64; the existing codegen test module checks
eBPF output in its bounded VM, including all input pairs at the signed boundary
and wraparound. Negative cases retain eBPF aggregate ABI, alignment and frame
limits. VM execution does not certify kernel verifier/JIT acceptance.

Wasm32 is not supported by the current driver. The external SPIR-V, NVPTX,
AMDGCN, Metal and DXIL pipelines accept different source-language/toolchain
contracts and reject native allocator options. They are **not covered** by this
canonical-C harness. Neither are booting UEFI images or mobile/device deployment.
GPU-specific source generation and device execution require separate adapters;
this suite does not claim that coverage.

The ordinary test suite runs one seed across all native allocator modes: 48
pairs, 96 compilations/executions, with no optional engine dependency. Desktop CI
gets this smoke coverage from its existing `test_all` jobs. Mobile builds retain
the preprocessing and harness self-tests and explicitly report native execution
as unavailable.

## Failure detection, reduction and artifacts

Compiler invocations and native/LLVM/Wasm execution use isolated child processes
with a ten-second deadline. The comparator checks phase, launch status, timeout,
process result, raw platform status, stdout and stderr. Equal crashes, equal
nonzero self-check exits and equal timeouts are failures, never evidence of
equivalence. Failure-class self-tests cover these cases and stream mismatches.
Each pair and reducer replay writes its sources, intermediate artifacts and
executables in a distinct `work-N` directory. The work index is claimed
atomically, so parallel work items within one campaign cannot collide, and a
later Windows compile never overwrites an executable image whose process has
only just exited. Each compilation still clears its expected output paths and
must produce a nonempty new artifact before consumption or execution. A compiler
that exits successfully without writing output cannot reuse a preceding Clang
reference executable or an earlier generated program.

For each distinct target/failure signature, up to 16 bundles are retained. The
same signature in another allocator is still counted and reported, but does not
repeat reduction. Each `failure-N` preserves the first observed outcomes and
available source/artifact files under `observed-*` names before attempting a
replay. Replay logs with exact argument vectors, `reproducer.txt`, and a
`minimized` directory are retained separately, so a flaky replay cannot erase
the initial failure evidence.

The reducer removes transformation bits, arithmetic terms and loops, simplifies
constants and reduces the input count. It regenerates **both** programs from the
same specification. A candidate is retained only after two replays preserve the
original phase/status/diagnostic signature and kind of output mismatch. Source
paths and locations are removed from compilation-diagnostic comparison so a
smaller source can keep the same diagnostic. At most 64 candidate replays are
allowed; timeout failures are saved without attempting expensive reduction.
This is bounded grammar-aware reduction, not a claim of global minimality for
arbitrary C. `signature_preserved` records the final replay result.

A five-minute campaign budget (one minute for ordinary smoke; five minutes when
the compiler itself is sanitized), checked between work
units, turns incomplete campaigns into failures. The current bounded pair or
reduction can finish after that budget; it is not a hard wall-clock supervisor
for the entire process. Pair scratch storage is rewound, while only the capped
set of unique failure signatures remains retained.

Reduced cases are artifacts, not automatically accepted compiler fixes. Check
that the reference compiler accepts the pair, reproduce the failure on unchanged
Buster, and add a permanent regression before changing production code. The
issue #220 fix, already merged separately through #240, keeps placemarkers through all `##` operations and removes them
only before rescanning. Its tests cover empty left/right/both operands, unrelated
preceding tokens, chained pastes, stringification spacing, GNU variadic comma
elision, and diagnostics for malformed pastes.

## Configuration and coverage reporting

`BUSTER_METAMORPHIC_SEED` is an unsigned 32-bit seed; `CASES` is 1–256;
`TRANSFORMS` is a nonzero subset of mask 2047. `COMPILER` selects another Buster
binary; `OUTPUT` selects the artifact directory. `TARGET` restricts the named
matrix row, with unknown names rejected. `REQUIRE_EXECUTION=1` makes any
compile-only pair fatal. `FRONTEND_SSA` accepts 0 or 1 and defaults to 1.
All these names have the `BUSTER_METAMORPHIC_` prefix.

`METAMORPHIC_REFERENCE` reports the actual reference command, optimization level
and pair count; `METAMORPHIC_REFERENCE_UNAVAILABLE` reports missing commands. Each `METAMORPHIC`
row reports target, allocator, pairs, executed, unexecuted and failed counts.
`METAMORPHIC_SUMMARY` reports total counts, unique failure bundles, reducer
replays and the resolved output directory. Save stdout with the bundles for a
complete campaign record. A zero exit without strict mode means no observed
failure in the reported coverage, not execution of unavailable targets.
