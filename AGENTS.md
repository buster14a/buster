# buster

This file is the shared repository guide for coding agents. `CLAUDE.md` imports
it. Keep always-needed rules here and command/module details in the linked
guides under `docs/commands/`; do not maintain parallel copies.

## Goal and scope

Build a new C compiler whose **implementation runs on Linux/x86-64, tuned for
AMD Zen 5**, with heavy, deliberate use of SIMD intrinsics. Optimize both
single-threaded compile throughput and useful multicore scaling while preserving
C correctness and generated-program quality.

Code should aim for the **target machine's maximum achievable throughput** on
the actual workload, measured as useful work completed per second, both per core
and across cores. Design algorithms and data layouts around the relevant
execution, dependency, cache, memory-bandwidth and I/O limits; eliminate
unnecessary computation, passes, copies, allocation and synchronization. Use
representative end-to-end measurements, hardware counters and generated assembly
to identify the limiting resource and validate progress toward that ceiling,
while preserving correctness and generated-program quality.

This is a fresh compiler, not a compatibility branch of `buster14a/buster-legacy`.
Do not inherit that repository's frameworks, historical token parsers, CI
matrices, benchmark services, or acceptance bureaucracy. Reuse an idea only when
it earns its place here. Do not copy implementation code without checking its
license; do not select or change this repository's license on the owner's behalf.

## Licensing and contributions

Read [LICENSING.md](LICENSING.md), [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)
and [CONTRIBUTING.md](CONTRIBUTING.md) before importing or submitting material.
Original contributions use `Apache-2.0 WITH LLVM-exception`; identified
components and local changes to their portions follow their retained terms.
Do not add the LLVM exception over Apache-only upstream material or label a
mixed-source file as wholly first-party.

Record the sources and revisions of imports, adaptations and generated data,
including generator inputs, and preserve copyright, license, attribution and
modification notices. Changes to the license remain the owner's decision.
Follow the prospective DCO policy and its in-flight-PR transition. Never
fabricate sign-offs, treat agent co-author trailers as human certification, or
retroactively certify historical commits. Add David's sign-off to a commit only
as "David's sign-off" below says.

## David's sign-off

Under "The maintainer's standing certification" in
[CONTRIBUTING.md](CONTRIBUTING.md), David's own agent sessions sign off their
commits for him with:

```text
Signed-off-by: David Gonzalez Martin <davidgmbb@gmail.com>
```

His opt-in gates it: `BUSTER_DCO_SIGN_OFF` set to `davidgmbb` in the session's
environment, which only he sets. Where it isn't set, never add that trailer.
Where it is:

- `.githooks/commit-msg` ends each message that `git commit` or `git merge`
  writes with it, once, after the other trailers, so don't write it yourself.
  Before your first commit, check that `git config core.hooksPath` prints
  `.githooks`; if it doesn't, run `.githooks/enable-sign-off.sh`. Claude Code
  runs it at session start (`.claude/settings.json`), and so does Codex
  (`.codex/hooks.json`) in a trusted project, once David has trusted that hook
  in `/hooks`. If it reports leaving the hook off, write the trailer yourself,
  last. While `core.commentChar` is `auto`, the hook refuses every commit, since
  it can't know the comment character `git commit` picks: set the character
  explicitly, or make the commit with the opt-in unset and the trailer written
  by hand.
- Don't use `git commit -s`, which signs off as the committer
  (`Claude <noreply@anthropic.com>` in cloud sessions), or `--no-verify`, which
  skips the hook. The hook signs nothing while a rebase or a cherry-pick is in
  progress, rewords, amends and continuations included, and a plain
  `git revert` doesn't run it: for a revert, use `git revert --no-commit` and
  then `git commit`.
- Don't sign a commit from before the certification took effect, or a rebase,
  cherry-pick or amend of one: it still needs David's certification for its
  PR's head. Unset the opt-in to amend one outside a rebase
  (`env -u BUSTER_DCO_SIGN_OFF git commit --amend`) or to commit its changes
  again another way (`git cherry-pick --no-commit`), and to commit outside
  material whose provenance the PR can't record; then say so in the PR.

## Host and output scope

The Zen 5 requirement describes the **host compiler executable**, not necessarily
the instruction set of every program it generates. Keep host ISA, generated-code
ISA, and output ABI separate. Linux/x86-64 hosts without the complete AVX-512
feature set use scalar implementations. `BUSTER_HOST_ARCH=x86-64` builds the
baseline executable; `native` is the default and `znver5` selects benchmark
builds. The host choice is made at build time, outside compiler hot paths.
Do not add other host operating systems. Broader output targets and extra
applications are outside the current scope; but the x86 encoder encodes 16- and
32-bit modes' instructions too, as the owner asked.

## Repository identity

The current compiler is `buster14a/buster`, GitHub repository ID `1421805693`,
formerly `buster14a/buster-fast`. The previous implementation remains maintained
at `buster14a/buster-legacy`, ID `1071732997`, with its own history, issues and
pull requests. Verify the destination ID when retargeting a checkout or writer;
old `buster14a/buster` links now address this compiler.

Local checkout directories, `~/buster-fast-bench/`, the shared host lock,
`~/llm-memory/projects/buster-fast/` and its memory symlinks retain their existing
names. The repository-local `buster-fast-9700x` concurrency key is also retained.
Queues use that prefix per PR/ref, each holding one pending run (a newer push
replaces its PR's, and cancels the PR's run in progress); the runner hooks serialize every CI job with
the shared host lock. These are operational resources, not canonical repository URLs; do not rename
them by replacing text. Source paths, executable names and object-file metadata
are unchanged by the repository rename.

## Attribution for agent-authored GitHub activity

Every agent session that writes substantive text to GitHub must identify **its
own session** and **the model used** in the GitHub item it posts. This includes
opening an issue or pull request; posting issue or PR comments, reviews,
inline review comments, or replies; and editing an existing issue or PR body.
Each new comment or review needs its **own** attribution, even when the issue
or PR already has one. Do not rely on a branch, commit message, prior comment,
or local record to identify the posting session.

Append this footer to every agent-authored GitHub body, with real values:

```text
---
Agent session: [private session](https://provider.example/private/session)
Model: Provider / exact model identifier
```

Where the provider exposes no shareable session URL, give its session
identifier and say that no URL is exposed instead:

```text
---
Agent session: ses_0123456789abcdef (Provider session ID; no share URL is exposed)
Model: Provider / exact model identifier
```

A session URL, where one exists, must be a **stable, authentication-gated
permalink** to the actual agent session, not a public transcript or an
`anyone with the link` share link. The URL text itself will be visible to
readers of the public GitHub issue or PR; privacy must come from the
provider's access controls. Do not include credentials, signed access URLs,
API tokens, private logs, or conversation contents. Record the exact
provider and model name/version the session reports; do not invent model
details. If the agent environment exposes a session identifier but no
suitable private session link, post the identifier with a note that no
share URL is exposed rather than silently omitting attribution or
publishing an unsafe link.

For posts involving several agent sessions, attribute each session and
its model when it contributed to that post. After a handoff, new comments,
reviews and edits must identify the session actually making them. This
provenance is separate from human authorship and DCO sign-off.

## Issue ownership

Raw measurement evidence lives in
[buster-performance/docs/measurements](https://github.com/buster14a/buster-performance/tree/main/docs/measurements).
Put new logs, captures, receipts, measurement harnesses and campaign artifacts
there. Keep design summaries and links here. Do not recreate `docs/measurements`
in this repository; the required `Measurement storage policy` check rejects
every tracked path there, including force-added ignored files. When updating an
older branch or PR, migrate its evidence to buster-performance before merging it.
CI consumers must pin external harnesses to an exact reviewed commit, and a
harness-pin update must enable the affected qualification in `9700x.yml`.

Every issue being investigated, tackled or fixed must be claimed in the issue
itself before work begins. Read its latest comments and linked pull requests,
then post a comment identifying who is taking the work and the scope being
claimed. A local plan, branch, pull request or assignee alone does not replace
this comment. When work covers several issues, claim each one.

Respect existing claims: coordinate with the current owner before taking over
or starting overlapping work. Keep the claim current with the branch or pull
request, and comment on the issue when handing off or stopping so others know
whether the work is still owned.

## Coding standards

Write code in **C** unless the goal or action explicitly disallows C or requires
another language. This applies to supporting tools, tests, and automation as
well as compiler code.

Follow [the C coding style](docs/coding-style.md) for new or modified compiler
code. The essentials are: C and Clang on Linux/Zen 5, 4-space Allman formatting,
`snake_case` functions, `PascalCase` types, `BUSTER_` macros, one return per
function, direct and explicit control flow, bounded slices, arena-oriented
allocation, and safe SIMD memory accesses. Prefer simple data layouts and
measured end-to-end performance improvements. Prefer single-line statements,
declarations, and function headers where readable, and one aggregate field or
initializer entry per line. Do not import buster-legacy's portability
or infrastructure machinery merely to match its style.

## Data-parallel first

Design every stage and every pull request data-parallel, vectorized and
branch-free first, as `src/buster/compiler/c_lexer.c` and `x86_64_encoder.c`
are built. The tools include masks over many items, prefix sums, in-register
permutes as table lookups, compress and expand, sorting and segmented scans,
bitsets, nearest-smaller-values and pointer jumping. Each vector path has an
exact scalar path for the rare cases, which is also its twin and test oracle:
both must give identical output or, where the two algorithms legitimately
differ, pass a semantic check.

No stage is assumed inherently sequential. Where C fixes an order (macro state,
typedef visibility), vectorize within each step and keep the ordered part small.
A part that stays scalar says why with measurements on the 9700X, as "SIMD: what
pays" in `docs/object-writer-and-linker.md` does. The owner set this rule on
2026-10-20, replacing the premise of
[#15](https://github.com/buster14a/buster/issues/15) that the parser and
the type checker are inherently sequential. The issues that apply it are listed
in the "Data-parallel first" section of
[#115](https://github.com/buster14a/buster/issues/115). "SIMD and Zen 5" in
[the C coding style](docs/coding-style.md) has the Zen 5 costs that shape these
designs and the compilers already built this way.

## Reading and maintaining this guide

Read the command/module guide relevant to your change before editing or testing
that area; do not load the whole guide directory into every session. The
[command index](docs/commands/README.md) links the detailed sections and their
existing design guides. Commands and source paths in those guides are relative
to the repository root. Keep detailed flags, fixtures, acceptance boundaries and
benchmark recipes in the owning guide, not in this file. A new command adds or
updates its guide and one compact index line here. `CLAUDE.md` imports only this
file. CI enforces a **30,000-byte** limit here and a 1,024-byte limit on that
import wrapper; do not raise the limits to accommodate module detail.

## Sharing the 9700X

Agent sessions and CI share the Ryzen 7 9700X. Run **every build, test, benchmark
and perf run there** through `EXCLUSIVE_OWNER=<name> ~/buster-fast-bench/exclusive.sh COMMAND...`.
Set `EXCLUSIVE_TITLE` to the session title and `EXCLUSIVE_LINK` to its private
session, PR or issue link. The runner hooks take the same lock; sessions and CI
wait alike, with neither taking priority. Hold it **per run, never around a
series of runs**: CI times out waiting after 10 minutes. Keep PR jobs short.
Benchmark timings come from this host under that lock, not from cloud runners
or the owner's desktop. The `cores` queue below does not replace this lock.

Read [CI and benchmark selection](docs/commands/ci.md) before changing gates,
suite inventory, benchmark steps or harness pins; it retains the full hosted
correctness, sanitizer, native qualification and measurement rules. For tuning,
also read [profiling](docs/commands/profiling.md) and [the CI guide](docs/ci.md).

## Sharing the owner's machines' cores

Where `cores` is on `PATH` (the owner's own machines; `cores help`), every
multicore run goes through it: builds (`./build.sh`), the parallel test runner,
lane tests and benchmarks, `--jobs` campaigns, clang oracles run at once. It is
a first-come, first-served queue of physical cores, not a lock on the machine:

```sh
CORES_TITLE="<session title>" CORES_LINK="<session, PR or issue>" cores -n 4 -- ./build.sh
cores --all -- ./build/compiler parallel < list
```

A job waits for every older one and for its cores, then runs pinned to both
threads of each, at niceness 19, with `CORES` set to its count, so tools that
size themselves by the CPUs they may run on (`nproc`, `parallel`'s `--jobs`,
lanes) fit the grant; pass `--jobs "$CORES"` or `--lanes "$CORES"` where a tool
takes a count. A single-threaded run that takes more than about 30 s (a fuzz
campaign, a test or oracle without `--jobs`, a long benchmark) goes through
`cores -n 1`, so that it neither shares a granted core's threads unseen nor
fills the pool past what `cores status` shows; shorter commands don't queue.
Ask for the cores the run can use, hold them per run rather than around a
series, and let a nested `cores` inside a job run at once on the job's cores. `cores status` shows who
holds and who waits; `cores away` and `cores here` are the owner's switch
between lending every core and keeping two for interactive use. None of this
replaces the 9700X's lock: benchmark timings still come from there.

## Agent memory

Agents keep what they learn about this project in the owner's private
`davidgmbb/llm-memory` repository, cloned at `~/llm-memory` on each of the
owner's computers:

- Claude Code's memory folder for this project is a symlink to
  `~/llm-memory/projects/buster-fast/`, so Claude saves its memories there as
  usual.
- Any other agent reads `~/llm-memory/projects/buster-fast/MEMORY.md` before
  starting and saves what later sessions will need there, in the same format:
  one fact per file, indexed in `MEMORY.md`. `~/llm-memory/AGENTS.md` has the
  rules, and Codex's automatic memories are copied to `~/llm-memory/codex/`.
- Don't save what this repository already records, and never save secrets.
- After saving, run `~/llm-memory/bin/llm-memory sync` to share the memory with
  the owner's other computers.

Where `~/llm-memory` doesn't exist, skip this.

## Build

Use `./build.sh`, with Clang on Linux/x86-64; it produces `build/compiler`,
including its tests and benchmarks. `BUSTER_HOST_ARCH=native` is the default;
use `x86-64` for the baseline scalar host and `znver5` for approved benchmark
builds. Respect the lock and `cores` rules above. `BUSTER_DEBUG_INFO=0` omits
debug information. Read [build details](docs/commands/build.md) before changing
translation units, build order, baked tables or code-placement experiments.
`build/` is disposable; never commit generated binaries or captures.

## Current repository and exact smoke commands

- `build/` is disposable output. Do not commit generated binaries or captures.

From the repository root, on the approved Linux Ryzen 7 9700X host:

```sh
./build.sh
clang -dM -E -march=native -fwrapv -funsigned-char -x c /dev/null > build/predefines.h
./build/compiler parse -fsyntax-only -march=native -Isrc -fwrapv -funsigned-char --predefines build/predefines.h src/buster/compiler/main.c
./build/compiler -fsyntax-only -march=native -Isrc -fwrapv -funsigned-char -fno-strict-aliasing -Wall -Werror src/buster/compiler/main.c
./build/compiler test-call-graph
./build.sh check-call-graph
./build/compiler test-profile
./build/compiler test-parallel
./build/compiler test-lexer
./build/compiler test-lexer --cplusplus
./build/compiler test-lexer --no-digraphs --no-line-comments
./build/compiler benchmark-lexer src
./build/compiler test-preprocessor-fuzz --std c17
./build/compiler test-preprocessor-fuzz --std c23
./build/compiler benchmark-preprocessor-fuzz
./build/compiler test-x86-encoder
./build/compiler test-baked-tables
./build/compiler benchmark-x86-encoder
./build/compiler test-branch-sizing --cross-check
./build/compiler benchmark-branch-sizing
./build/compiler test-x86-semantics --cross-check
./build/compiler benchmark-x86-semantics
./build/compiler test-lanes
./build/compiler benchmark-lanes
./build/compiler test-dataflow
./build/compiler benchmark-dataflow
./build/compiler test-link
./build/compiler test-object-writer --cross-check
./build/compiler benchmark-object-writer
./build/compiler test-import --cross-check
./build/compiler benchmark-import
./build/compiler test-bind --cross-check
./build/compiler benchmark-bind
./build/compiler test-executable
./build/compiler test-inflate
./build/compiler test-dynamic --cross-check
./build/compiler test-static --cross-check
./build/compiler benchmark-executable
./build/compiler test-snapshot
./build/compiler benchmark-snapshot
./build/compiler benchmark-link
./build/compiler benchmark-floors
./build/compiler test-foreign --cross-check
./build/compiler test-foreign-corpus
./build/compiler benchmark-foreign --linkers
./build/compiler test-refinement src
./build/compiler benchmark-refinement src
./build/compiler test-preprocessor --clang
./build/compiler test-preprocessor --tables src
./build/compiler benchmark-preprocessor --tables src
./build/compiler -E -march=native -Isrc -fwrapv -funsigned-char -o build/main.i src/buster/compiler/main.c
./build/compiler benchmark-preprocessor --clang -march=native -Isrc -fwrapv -funsigned-char src/buster/compiler/main.c
./build/compiler benchmark-preprocessor --embed 16777216
./build/compiler test-files
./build/compiler test-files --flat src
./build/compiler test-files -march=native -Isrc -fwrapv -funsigned-char --predefines build/predefines.h src/buster/compiler/main.c src/buster/compiler/*_benchmark.c
./build/compiler benchmark-files -march=native -Isrc -fwrapv -funsigned-char --predefines build/predefines.h src/buster/compiler/main.c src/buster/compiler/*_benchmark.c
./build/compiler test-parser --clang
./build/compiler test-parser --clang -march=native -Isrc -fwrapv -funsigned-char --predefines build/predefines.h src/buster/compiler/main.c
./build/compiler benchmark-parser -march=native -Isrc -fwrapv -funsigned-char --predefines build/predefines.h src/buster/compiler/main.c
./build/compiler test-types
./build/compiler benchmark-types
./build/compiler test-checker --seed 133
./build/compiler benchmark-checker --shape wide
./build/compiler benchmark-checker --shape chain --nodes 4096
./build/compiler test-calling-convention --cross-check
./build/compiler benchmark-calling-convention
./build/compiler test-tier0 --cross-check
./build/compiler benchmark-backend
./build/compiler test-check-debug --cross-check
./build/compiler benchmark-check-debug
./build/compiler test-check-lanes
./build/compiler test-check-units
./build/compiler test-ir
./build/compiler test-ir-lowering
./build/compiler benchmark-ir
./build/compiler benchmark-ir-lowering
./build/compiler test-c-testsuite --self-test
git init -q --template= build/c-testsuite
git -C build/c-testsuite fetch -q --depth 1 https://github.com/c-testsuite/c-testsuite 5c7275656d751de0e68b2d340a95b5681858ed07
git -C build/c-testsuite checkout -q FETCH_HEAD
./build/compiler test-c-testsuite --cross-check --subset tests/c-testsuite/subset.txt build/c-testsuite
./build/compiler test-ssa-source $(sed -n 's|^\([0-9][0-9]*\)$|build/c-testsuite/tests/single-exec/\1.c|p' tests/c-testsuite/subset.txt)
./build/compiler test-ssa-source --generate 48
./build/compiler benchmark-backend-source build/c-testsuite/tests/single-exec/*.c
```

The compiler's host path follows the target feature macros, which the
preprocessor's predefines give as clang's do for the same options
(`c_predefines.c`). A baseline `-march=x86-64` build selects the scalar host path.

## Command and module index

Source entry points below are relative to `src/buster/compiler/`, except
`tools/` paths. Each guide contains the full test/benchmark commands, owning
files, flags and acceptance boundaries; the entries here are compact starting
points. [Detailed section index](docs/commands/README.md#detailed-sections).

- `test-assembler, benchmark-assembler` — `assembler.c` — [guide](docs/commands/assembler.md).
- `test-bind, benchmark-bind` — `bind.c` — [guide](docs/commands/bind.md).
- `test-bindings, test-parser-bindings` — `c_bindings.c` — [guide](docs/commands/bindings.md).
- `test-branch-sizing, benchmark-branch-sizing` — `branch_sizing.c` — [guide](docs/commands/branch-sizing.md).
- `./build.sh, bake-tables, test-baked-tables` — `baked.c` — [guide](docs/commands/build.md).
- `test-builtins, test-check-builtins, test-builtin-lowering` — `c_builtin.c` — [guide](docs/commands/builtins.md).
- `check-call-graph, test-call-graph` — `call_graph.c` — [guide](docs/commands/call-graph.md).
- `test-calling-convention, benchmark-calling-convention` — `calling_convention.c` — [guide](docs/commands/calling-convention.md).
- `check, test-checker, test-checker-driver` — `c_checker.c` — [guide](docs/commands/checker.md).
- `test-checker-asm` — `c_check_program_asm.c` — [guide](docs/commands/checker-asm.md).
- `test-checker-corpus` — `c_check_corpus.c` — [guide](docs/commands/checker-corpus.md).
- `test-check-debug, benchmark-check-debug` — `c_check_debug.c` — [guide](docs/commands/check-debug.md).
- `correctness.tests / CI selection` — `tools/ci/correctness.tests` — [guide](docs/commands/ci.md).
- `measure-compile, --time-stages` — `c_check_clock.c` — [guide](docs/commands/compile-time.md).
- `test-c-testsuite` — `c_testsuite.c` — [guide](docs/commands/c-testsuite.md).
- `test-dataflow, benchmark-dataflow` — `dataflow.c` — [guide](docs/commands/dataflow.md).
- `test-debug, benchmark-debug` — `debug.c` — [guide](docs/commands/debug.md).
- `test-checker-deferred, benchmark-checker-deferred` — `c_check_deferred_test.c` — [guide](docs/commands/deferred.md).
- `test-diagnostics, benchmark-diagnostics` — `diagnostic.c` — [guide](docs/commands/determinism.md).
- `test-differential, test-differential-harness` — `differential.c` — [guide](docs/commands/differential.md).
- `test-dominators, benchmark-dominators` — `dominators.c` — [guide](docs/commands/dominators.md).
- `-E, parse, -fsyntax-only, -c` — `main.c` — [guide](docs/commands/driver.md).
- `test-executable, test-dynamic, test-static` — `executable.c` — [guide](docs/commands/executable.md).
- `test-files, benchmark-files` — `c_files.c` — [guide](docs/commands/files.md).
- `benchmark-floors` — `link_benchmark.c` — [guide](docs/commands/floors.md).
- `test-foreign, benchmark-foreign` — `foreign.c` — [guide](docs/commands/foreign.md).
- `test-foreign-corpus` — `foreign_corpus_test.c` — [guide](docs/commands/foreign-corpus.md).
- `link-foreign (DWARF)` — `foreign_debug.c` — [guide](docs/commands/foreign-debug.md).
- `link-foreign` — `foreign_driver.c` — [guide](docs/commands/foreign-driver.md).
- `benchmark-foreign-gc` — `foreign_gc.c` — [guide](docs/commands/foreign-gc.md).
- `benchmark-foreign-strings` — `foreign_strings.c` — [guide](docs/commands/foreign-strings.md).
- `benchmark-foreign-unwind` — `foreign_unwind.c` — [guide](docs/commands/foreign-unwind.md).
- `benchmark-foreign-unwind-dedup` — `foreign_unwind_dedup.c` — [guide](docs/commands/foreign-unwind-dedup.md).
- `test-check-lanes, benchmark-check-lanes` — `c_check_program_emit.c` — [guide](docs/commands/function-lanes.md).
- `test-gnu, benchmark-gnu` — `c_gnu.c` — [guide](docs/commands/gnu.md).
- `test-import, benchmark-import` — `import.c` — [guide](docs/commands/import.md).
- `test-ir, test-ir-lowering, benchmark-ir` — `ir.c` — [guide](docs/commands/ir.md).
- `test-ir-optimize, test-ir-gvn, test-ir-licm` — `ir_optimize.c` — [guide](docs/commands/ir-optimization.md).
- `test-ir-parse, reduce-ir, interpret-ir` — `ir_parse.c` — [guide](docs/commands/ir-tools.md).
- `test-lanes, benchmark-lanes` — `lane.c` — [guide](docs/commands/lanes.md).
- `test-lexer, benchmark-lexer` — `c_lexer.c` — [guide](docs/commands/lexer.md).
- `test-link` — `link.c` — [guide](docs/commands/link.md).
- `benchmark-link` — `link_benchmark.c` — [guide](docs/commands/link-benchmark.md).
- `test-lowering, benchmark-lowering-source` — `c_lowering.c` — [guide](docs/commands/lowering.md).
- `test-object-writer, benchmark-object-writer` — `object_writer.c` — [guide](docs/commands/object-writer.md).
- `test-oracle-cache, BUSTER_ORACLE_CACHE` — `oracle_cache.c` — [guide](docs/commands/oracle-cache.md).
- `parallel, test-parallel` — `parallel.c` — [guide](docs/commands/parallel.md).
- `test-parser, benchmark-parser` — `c_parser.c` — [guide](docs/commands/parser.md).
- `test-preprocessor, benchmark-preprocessor` — `c_preprocessor.c` — [guide](docs/commands/preprocessor.md).
- `test-preprocessor-fuzz, benchmark-preprocessor-fuzz` — `preprocessor_fuzz.c` — [guide](docs/commands/preprocessor-fuzz.md).
- `ab, time-runs, test-profile` — `profile.c` — [guide](docs/commands/profiling.md).
- `test-refinement, benchmark-refinement` — `c_intern.c` — [guide](docs/commands/refinement.md).
- `test-snapshot, benchmark-snapshot` — `snapshot.c` — [guide](docs/commands/snapshot.md).
- `test-ssa, test-ssa-ir, test-ssa-source` — `ssa.c` — [guide](docs/commands/ssa.md).
- `test-streaming, benchmark-streaming` — `streaming.c` — [guide](docs/commands/streaming.md).
- `test-tier0, benchmark-backend, benchmark-backend-source` — `tier0.c` — [guide](docs/commands/tier0.md).
- `test-tier1-select, test-tier1-pipeline, test-tier1-masks` — `tier1_select.c` — [guide](docs/commands/tier1.md).
- `test-tier1-allocation, benchmark-tier1-allocation` — `tier1_allocate.c` — [guide](docs/commands/tier1-allocation.md).
- `allocation certificate checks` — `tier1_certificate.c` — [guide](docs/commands/tier1-certificate.md).
- `test-tier1-cfg, test-tier1-cfg-source` — `tier1_cfg.c` — [guide](docs/commands/tier1-cfg.md).
- `test-tier1-frame, test-tier1-frame-execution` — `tier1_frame.c` — [guide](docs/commands/tier1-frame.md).
- `test-tier1-schedule, benchmark-tier1-schedule` — `tier1_schedule.c` — [guide](docs/commands/tier1-schedule.md).
- `test-check-units` — `c_checker_driver.c` — [guide](docs/commands/translation-units.md).
- `test-types, benchmark-types` — `c_types.c` — [guide](docs/commands/types.md).
- `test-unwind, benchmark-unwind` — `unwind_tier0.c` — [guide](docs/commands/unwind.md).
- `disassemble, test-x86-disassembler` — `x86_64_disassembler.c` — [guide](docs/commands/x86-disassembler.md).
- `test-x86-encoder, benchmark-x86-encoder` — `x86_64_encoder.c` — [guide](docs/commands/x86-encoder.md).
- `test-x86-intrinsics` — `x86_intrinsics_test.c` — [guide](docs/commands/x86-intrinsics.md).
- `test-x86-semantics, benchmark-x86-semantics` — `x86_semantics.c` — [guide](docs/commands/x86-semantics.md).
- `test-x86-target, test-x86-target-selection` — `x86_target.c` — [guide](docs/commands/x86-target.md).
- `test-x86-zen5, measure-x86-zen5, measure-mask-spills` — `x86_zen5.c` — [guide](docs/commands/x86-zen5.md).
