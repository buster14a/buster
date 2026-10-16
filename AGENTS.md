# buster-fast

This file is the shared repository guide for coding agents. `CLAUDE.md` imports
it; keep project instructions here rather than maintaining parallel copies.

## Goal and scope

Build a new C compiler whose **implementation runs on Linux/x86-64, tuned for
AMD Zen 5**, with heavy, deliberate use of SIMD intrinsics. Optimize both
single-threaded compile throughput and useful multicore scaling while preserving
C correctness and generated-program quality.

This is a fresh compiler, not a compatibility branch of `buster14a/buster`.
Do not inherit that repository's frameworks, historical token parsers, CI
matrices, benchmark services, or acceptance bureaucracy. Reuse an idea only when
it earns its place here. Do not copy implementation code without checking its
license; do not select or change this repository's license on the owner's behalf.

The Zen 5 requirement describes the **host compiler executable**, not necessarily
the instruction set of every program it generates. Keep host ISA, generated-code
ISA, and output ABI separate. Do not add other host operating systems, older-CPU
production fallbacks, or hot-path CPU dispatch. Broader output targets and extra
applications are outside the current scope.

## Current repository and exact smoke commands

- `src/buster/compiler/main.c` is the executable entry point. It currently prints
  `Hello world`; it does not yet parse or compile C input.
- `build.sh` invokes Clang and produces `build/compiler`. It currently uses
  `-O3 -march=native`, so the machine executing the build determines the host ISA.
- `build/` is disposable output. Do not commit generated binaries or captures.

From the repository root, on the approved Linux Ryzen 7 9700X host:

```sh
./build.sh
./build/compiler
```

For now, CI does exactly this build and execution, plus checkout and a host/tool
identity check. A successful run is a **smoke test**, not evidence of C-language
coverage, self-hosting, SIMD coverage, or a performance improvement. Keep these
commands and the workflow synchronized as the CLI evolves; do not invent flags
or input files that the compiler does not support.

Use the existing shell build. Do not introduce CMake, Ninja, package managers,
Python tooling, a generated build framework, or an expanded test matrix unless
requested. Implementation and substantive tools belong in C; Bash is small
build/CI glue and YAML is for GitHub Actions. The existing use of libc and an
installed bootstrap compiler is intentional, not a freestanding requirement.
For controlled builds off the measurement host, explicit `-march=znver5` is the
intended ISA target; changing the actual build command is a separate code change,
not permission to silently substitute a different ISA in a comparison.

## Architecture direction

These are implementation goals, not claims about the current placeholder.
Deliver runnable vertical slices rather than scaffolding all stages in advance.

1. **One grammar authority.** Process translation, lexing and macro expansion in
   contiguous batches with correct ordering, expansion state and source
   provenance. Parse expressions, declarations, declarators, statements and
   initializers once. Semantic completion and lowering consume syntax nodes,
   bindings and retained facts, never token ranges as a hidden reparse API.
2. **Compact syntax and facts.** Prefer append-built, flat postorder syntax with
   compact subtree extents and stable integer identities. Use selective child
   metadata where justified. Preserve declaration-point visibility, typedef
   shadowing, incomplete-type rules, unevaluated contexts and diagnostics.
   Compute reusable type, conversion, layout and constant facts when their
   dependencies are ready. Do not repeatedly speculate over whole semantic
   models or duplicate facts merely to cross a phase boundary.
3. **Function-owned canonical SSA.** Use one typed canonical IR and one target
   machine representation. Keep common rows compact and exceptional operands,
   analyses and provenance in side storage. A worker owns a function's mutable
   state through consecutive semantic, lowering and backend kernels. A logical
   phase does not automatically require another full-unit pass, allocation or
   thread handoff. Measure simple separate traversals against traversal fusion.
4. **Global optimization has an explicit boundary.** For optimizing modes,
   publish canonical functions and compact call/reference/effect summaries,
   make interprocedural decisions, then transform and compile independent
   functions or partitions in parallel. Use immutable callee inputs and one
   writer per caller. Track analysis invalidation after changes; bound inlining,
   cloning and propagation without treating unfinished guesses as facts.
   Respect linkage, unknown callers and observable effects. Do not sacrifice
   optimization correctness to claim compilation speed.
5. **Ownership and lifetime are explicit.** Prefer flat arrays, local arenas and
   stable integer references to per-node allocation and pointer-rich graphs.
   Retain only data with a consumer. Check allocation/index arithmetic, growth
   and tail accesses. Input-dependent nesting must not exhaust the C call stack;
   use explicit work stacks where needed. Errors must not publish valid-looking
   partial output.

## Zen 5 and SIMD

SIMD is a first-class implementation technique, not a lexer-only optimization.
Design hot storage so a kernel can load a block, classify/filter it, produce a
mask and process or compact selected elements. Candidate areas include source
classification, identifier-table fingerprints, literal conversion, opcode scans,
bitsets, liveness operations, candidate extraction and index remapping.

Use `<immintrin.h>` and a small concrete intrinsic/builtin layer when useful.
Do not build a cross-platform vector abstraction. Keep the builtin surface
explicit so eventual self-hosting does not accidentally require every bootstrap
compiler intrinsic. Preserve lane, mask, immediate and memory semantics.

Choose vector width, block size, SoA/AoS layout and scalar control work by actual
cost. Scalar handling of tiny inputs or irregular decisions is allowed; a
portable production fallback is not required. Test-only scalar references are
useful independent oracles. Do not interpret wide execution resources as a
universal instruction-throughput guarantee or assume branchless code wins.
Inspect assembly and instruction-form measurements before making hardware claims.

Never overread or overwrite an allocation. Use documented owned padding or
correct masked tails; test alignment, all tail lengths and page boundaries as
kernels arrive. Integer overflow, aliasing and alignment assumptions must be
explicit and valid, not accidental bootstrap-compiler behavior.

## Multithreading

Use persistent workers/lanes with direct kernels. Prefer translation units and
functions as work units; batch tiny functions and use dynamic index claims where
size imbalance warrants them. Do not introduce a general callback task framework,
a thread per stage, or SIMD lanes executing unrelated parser state machines.

The one-worker path uses the same kernels directly, without mandatory queue
round trips. Keep one concurrency budget across translation units and functions;
never multiply nested worker counts. Bound outstanding work by memory as well as
item count. Keep mutable scopes, IR and scratch worker-local; publish immutable
shared inputs with a correct synchronization protocol. Do not share mutable
interners or counters through unprotected accesses.

Assign identities and output slots independently of completion order. Worker
count must not alter program semantics, diagnostics or artifact bytes. Global
optimization decisions and budgets must not depend on which worker arrives first.
Add producer/consumer overlap only with explicit source-point and lifetime rules;
never rediscover syntax to manufacture more parallel work.

## CI and the physical 9700X

`.github/workflows/9700x.yml` runs on pushes to `main` and manual dispatch from
`main`. It deliberately has no pull-request or `pull_request_target` trigger:
this is a public repository and the physical machine must not execute unreviewed
fork code. Check out the triggering commit, not a moving branch or arbitrary input.

The job selects runner group `buster-9700x-service-dispatch` and labels
`self-hosted`, `Linux`, `X64`, `buster-zen5`, `ryzen-9700x`. It checks the actual
Linux/x86-64 CPU model before building. The runner needs Git, Bash, Clang and a
GitHub Actions runner supporting the pinned checkout action's Node 24 runtime
(runner 2.327.1 or later). CI must not install system packages, use sudo, persist
checkout credentials, or request write permissions/secrets.

Runner access is organization configuration, not something YAML grants. The
existing group must permit this public repository and this exact workflow:

```text
buster14a/buster-fast/.github/workflows/9700x.yml@refs/heads/main
```

Keep repository/workflow access narrowly selected. Do not broaden access to all
repositories/workflows, change other projects' jobs, or re-register the machine
as a workaround. An access failure or queued job is not a passing hardware test.
The workflow serializes its own runs without cancelling active ones; this does
not coordinate other repositories or provide benchmark isolation.

Ordinary agent editing and investigation stay on cloud infrastructure, not the
owner's laptop or physical benchmark host. The 9700X workflow is the explicitly
requested build/execution route. Do not add benchmarks, stress suites, sanitizers,
artifact services or additional hardware jobs to this initial smoke workflow
without a concrete request.

## Working and reporting

Keep changes small and directly tied to the task. Use one writer per branch;
inspect the current head and preserve concurrent owner changes. Do not import
unrelated Buster policy or rewrite owner-authored code as incidental cleanup.

As features arrive, add focused C correctness tests and independent valid/invalid
expectations. Preserve C preprocessing order, scope, qualifiers, evaluation order,
short-circuit behavior, target layout and exactly-once effects. A baseline compiler
can have bugs; matching it alone is not proof. Add sanitizer/fuzz/race coverage as
separate requested work, not placeholder infrastructure in this bootstrap CI.

For performance work, record exact source, compiler, flags, inputs, CPU and worker
count. Measure single-worker work and multi-worker latency separately; keep
correctness and generated-code quality equivalent. Charge allocations, copies,
first touches, synchronization and publication to the result. Use matched repeated
runs and report uncertainty and regressions. Instruction counts are not wall time,
and a SIMD microbenchmark is not an end-to-end compiler result.

Report what changed, the exact commands actually executed, their outcomes and
anything not verified. Never call a queued/skipped workflow green or claim that
this repository's placeholder already implements the architecture above.
