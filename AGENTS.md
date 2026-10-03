# Agent instructions

**Buster** is a personal monorepo of applications, shared C components, tools
and experiments. The compiler and toolchain is one project; find the others
in [PROJECTS.md](PROJECTS.md). Its `ide` target is the headless compiler, test
runner, benchmark driver and metadata tool; the name is retained for build
compatibility. C is the compiler's sole active source frontend.

This file contains the rules needed for everyday work. Read the matching
[topic references](#topic-references) before changing a subsystem; search their
headings and symbols first instead of loading every guide. Paths and commands
are relative to the repository root. When documentation and implementation
disagree, inspect the code and update the affected guide in the same change.

## Priorities and boundaries

- For compiler work, optimize **compiler throughput and time to an artifact**.
  Do not add passes or spend compile time improving generated programs unless
  requested. Other projects keep their own documented behavior and validation
  contracts; catalogue membership does not impose compiler benchmarks on them.
- Prefer compact contiguous data, explicit counts, indices, masks, homogeneous
  batches, and precomputed facts. Organize and measure the workload before
  choosing instructions; fewer misses or SIMD readiness alone is not a speedup.
- Preserve the pipeline: C preprocessing/parsing/semantics -> canonical IR ->
  native machine code or a direct non-native backend. Do not introduce another
  frontend IR or bypass canonical IR. Validate IR before backend consumption.
- Keep canonical IR independent of frontend IDs and AST pointers. Retain the
  translation-unit arena until every downstream consumer finishes. Invalid
  source needs structured diagnostics and a failed result, never an internal
  assertion, `BUSTER_TODO()`, or apparently valid partial output.
- The custom-language compiler is intentionally removed. `.bbb` fixtures and
  [DORMANT_CUSTOM_COMPILER.md](DORMANT_CUSTOM_COMPILER.md) are preservation
  material; do not reactivate them in builds, tests, benchmarks, packaging, or CI.
- Do not edit or archive generated `build/` output as source. Keep changes
  focused, preserve other work, and check current issues/PRs before duplicating it.

## Core rules

- **C only; no new dependencies or vendored code.** Preserve `-fwrapv`,
  `-fno-strict-aliasing`, and `-funsigned-char`. Warnings are errors; keep Clang,
  GCC, Zig cc, and MSVC builds working. TCC bootstraps `build.c` only.
- **One return per function.** Converge through structured control flow to one
  final return. Mutually exclusive preprocessor returns count as one. Do not
  introduce a `goto` tail to simulate early returns; only existing error-unwind
  ladders are exempt. Do not initialize a result every path already assigns.
- **No input-dependent recursion.** Use an explicit stack, queue, or worklist;
  only trivial, statically bounded recursion is allowed.
- **No compiler callbacks/function-pointer dispatch**, except the uniform
  `lane_run` entry. Use direct calls, loops, switches, and explicit work data.
- Use arena allocation, `String8`/`S8`, explicit struct typedefs, and `BUSTER_` macros.
  Match surrounding style: four spaces, snake_case, braces on their own line,
  and single-line declarations/statements where clear. Include headers through
  `<buster/lib/...>` or `<buster/tests/...>`.
- `BUSTER_F_DECL` belongs only on header declarations; `.c`-local functions use
  `BUSTER_GLOBAL_LOCAL`, and header-declared definitions carry no linkage macro.
- Major files need an orientation header with ownership, entry points, and a
  map using searchable symbols, not line numbers. Update it when moving code.
  Name shared constants; comments explain verified constraints.
- Parallel work uses the existing persistent lane gang: SPMD across threads,
  SIMD within a lane, stable work-indexed results, and deterministic merges.
  One-lane and `BUSTER_SINGLE_THREADED` builds use the same path. Prewarm shared
  tables serially before `lane_run`; guard initialization with
  `BUSTER_CHECK_SERIAL_INITIALIZATION()` and publish readiness last.
- Write SIMD kernels using `<buster/lib/simd.h>` with feature/compiler guards
  and correct fallbacks. Its operations are macros: arguments must have no side
  effects. Design for Zen 5's 512-bit width, validate the Zen 4 tradeoff, and
  preserve MSVC, AArch64, and self-hosting. Read the [SIMD guide](docs/agents/simd.md)
  before changing kernels and the [parallelism guide](docs/agents/parallelism.md)
  before changing concurrency or shared initialization.
- Modules consumed by `ide` must be registered in CMake, added to its module
  list, and included in its unity block. Other applications register their own
  dependencies; do not pull unused modules into the compiler. Wire shared tests
  into the CMake test source/header lists and `src/buster/tests/test.c`; keep test
  declarations behind `BUSTER_INCLUDE_TESTS` and private seams in `*_internal.h`.

## Build and validation

`build.sh` / `build.ps1` bootstrap and reuse an immutable native `build.c`
driver with TCC; that driver owns build policy and orchestration. CMake
generates the graph and Ninja executes it. Keep workflows out of shell,
PowerShell, and CMake scripting.
GitHub-hosted Clang bootstrapping is a documented CI exception, not a trusted
local-bootstrap substitute; see [build guidance](docs/agents/build.md).

Run from the repository root. Configure a fresh tree once, then build it:

```sh
./build.sh generate
./build.sh build --config Release -t ide
```

**`generate` deletes and recreates its selected build directory.** Never run it
alongside a build, self-host, benchmark, or harness using that directory. On an
existing configured tree, use `build` for incremental work. `--sanitize`,
`--fuzz`, `--lto`, `--ci`, `--time-trace`, `--instrument`, and `--cc` belong to
`generate`, not `build`. On Windows use `./build.ps1` with the same arguments.

| Change or goal | Validation from the repository root |
|---|---|
| Before and after compiler changes | `./build.sh test_self_host --config Release` — reproduce the baseline and preserve the byte-identical fixed point. |
| Production behavior | Add a focused regression, then `./build.sh build --config Release -t test_all`. |
| Growing production or build code | Commit, then `./build.sh source_size --base "$(git merge-base origin/main HEAD)"`; past 32 KiB, acknowledge per [source-size policy](docs/source-size.md). |
| Allocator, ABI, or backend changes | `./build.sh test_mode_matrix --config Release`; cover `none`, `mir-stack`, `fast`, and `quality` as applicable. |
| Sanitized validation | With the build directory idle: `./build.sh generate --sanitize`, then `./build.sh build -t test_all`. |
| Full local compiler/configuration matrix | `./build.sh test_all_combinations`. |
| External compatibility work | Read the [harness index](docs/agents/compatibility.md); use its pristine pinned inputs and affected harness. |
| Documentation only | Check commands against their implementation, local links, and `git diff --check`; compiler tests are unnecessary unless behavior also changes. Raw audit evidence is byte-exact and [exempt from the whitespace check](docs/agents/benchmarking.md#performance-audit-notes); audit prose is not. |

Preserve Debug/Release, unity/non-unity, sanitizer/fuzz, self-host, and supported
platform coverage. Routine Apple CI validates AArch64 only; Apple x86-64
source/target compatibility is best-effort, with no Intel or universal release
validation promise. See [Apple CI policy](docs/apple-ci-policy.md). Test fixtures use repository-relative paths; concurrent tests
honor `BUSTER_TEST_JOBS`. Report the actual revision, commands, results, and
unavailable gates; never call an unrun check green. Follow the existing
[rebase validation workflow](docs/agents/workflow.md) when rebasing a code change.

Native-retirement generated source identities and the aggregate binding are
integration-owned. Ordinary feature PRs must not refresh or commit them;
the read-only gate reconstructs them ephemerally, ordinary PRs land through
the native queue, and an automatic catch-up publishes the pair afterwards.
Trust transitions still go through the trusted writer first. Read
[native-retirement rebinding](docs/native-retirement-rebinding.md) before
changing its policy, authority code, schema, consumers, or workflows.

## Benchmarking and diagnostics

Before performance work, read the newest audit (`tools/new_audit.py --newest`
prints its path; [PERFORMANCE_AUDITS.md](PERFORMANCE_AUDITS.md) explains the
history), then the relevant methods in
[benchmarking.md](docs/agents/benchmarking.md). Measure using the trusted
Clang-built compiler; self-built stages validate the fixed point. Compare the
same inputs, flags, target, and machine. Report compile time and useful work,
not just a proxy or generated-program runtime.
For a compiler change, run `tools/uarch_lab.py compare` (A/B; `run`
profiles one binary) and read the verdict in its `summary.json`.

Record an audit with `tools/new_audit.py`: it writes one new file under
`docs/performance-audits/` and nothing else. Never add a line to the closed
index in `PERFORMANCE_AUDITS.md`, and never rewrite an existing audit.

## Project license reporting

When scanning, researching, auditing, or comparing projects, include each
project's license in the report and any resulting GitHub issue or PR. Verify
it from license files or authoritative repository metadata at the inspected
revision, and cite the source; include the SPDX identifier when available.
Distinguish dual/multiple licenses and relevant component-specific licenses
rather than presenting one license as covering everything. Explicitly mark
missing, ambiguous, or unverified licenses; do not guess from public access.

## Forge, issues, and pull requests

Identify the owning project/component and existing issue before work. Follow
[project and feature tracking](docs/project-tracking.md): capability changes
update the owning documentation in the same PR; before handoff record the
branch/PR, exact revision, actual validation, unresolved findings and next action
on the issue/PR. Keep live progress out of PROJECTS.md and preserve the existing
[research lifecycle](docs/agents/research.md).

Before editing, read the owning issue's latest comments and all-state matching
PRs, then record the planned branch and bounded scope on the issue. Parallel
sessions use those claims or an existing shared ledger to reserve independent
work; an idle session is not evidence that its branch is abandoned. Keep one
writer per branch. Resolve overlapping ownership before creating a competing
fix, and record an intentional merge order when separate branches touch the
same contract.

After publishing, continue watching the exact-head checks and reviews through
integration within the authorized task. Before pausing or ending with an open
PR, leave a handoff on the issue/PR naming its next owner, exact revision,
completed validation, outstanding gates and next action. If no successor has
accepted ownership, say so explicitly; publishing a PR does not complete a bug.

Use the repository/host the user names; for a GitHub URL, work on that GitHub
repository. Otherwise inspect the current remote. Historical Forgejo issue
records and the retired source-free runner broker design remain available;
active GitHub infrastructure rules are in [workflow.md](docs/agents/workflow.md)
and [testing.md](docs/agents/testing.md).
Check live CI for the submitted commit instead of relying on old reports.

Historical issue references may use Forgejo numbers. Resolve those through
[docs/issue-migration-map.md](docs/issue-migration-map.md) or
[docs/forgejo-issue-archive.md](docs/forgejo-issue-archive.md); a current GitHub
URL already identifies its issue and must not be remapped.

**Branch naming.** Read the task or issue before choosing a new branch name.
When you control the name, use `<agent>/<issue-number>-<short-description>`,
or `<agent>/<short-description>` when no issue exists; never invent an issue
number. Use `claude/` for Claude and the established prefix for other agents.
Use lowercase kebab-case for the description and name the actual change,
not the activity of working on it. Examples (illustrative issue numbers):
`claude/123-fix-msvc-preflight`, `claude/456-reduce-merge-queue-concurrency`,
and `claude/document-native-abi`. Do not choose random adjective/person/animal
names, generic `fix-issues` or `implement-changes` names, or unnecessary dates
and identifiers.

Preserve any prefix or session suffix required by the execution platform.
If the platform has already assigned the working branch, keep it when renaming
is unsupported or would break session/push restrictions; do not bypass those
restrictions for cosmetic naming. Do not rename an existing PR branch or
another session's branch merely to satisfy this convention. If a new name is
already taken, choose a meaningful task-specific qualifier; never reset or
reuse another session's branch.

Write issue/PR bodies using structured arguments or a body file. Do not interpolate
Markdown into shell commands. Use `--force-with-lease`, never bare `--force`,
when an authorized rebase requires a force-push.

Report every actionable problem discovered during a task on this repository's
GitHub forge before ending the session or handing off. This includes defects, missing
coverage, CI or infrastructure failures, and blockers, even when the assigned
task is different. Search open and closed issues and PRs first; add new evidence
to the relevant issue or PR instead of opening a duplicate. If no record exists,
create an issue with the exact revision, observed versus expected behavior,
reproduction or source evidence, affected symbols, validation already run,
remaining uncertainty, and completion criteria. Mark hypotheses as unverified;
do not present a failed experiment or an unrelated transient as a confirmed
defect. Link the report from the active PR or handoff. If GitHub writing is
unavailable, retain a ready-to-post report in the handoff and say explicitly
that it has not been published. Do not silently drop a finding because it is
outside the current task.

## Topic references

Read only what the task touches. Detailed compatibility and frontend notes
contain historical investigations as well as contracts; reproduce old results
against the current revision before treating them as current limitations.

| Area | Guide |
|---|---|
| Projects, features and work tracking | [Catalogue](PROJECTS.md) and [tracking](docs/project-tracking.md) |
| Source paths and application entrypoints | [Project and repository map](docs/agents/project.md) |
| Build driver, configuration flags, self-host, matrix scheduling | [Build](docs/agents/build.md) |
| Module/test registration, platform CI, broker restrictions | [Tests and CI](docs/agents/testing.md) |
| External library/application harnesses | [Compatibility index](docs/agents/compatibility.md) |
| C style, one-return details, linkage macros | [Coding rules](docs/agents/style.md) |
| Data layout, SIMD kernels, lexer method, builtin extension checklist | [SIMD](docs/agents/simd.md) |
| Lane model, deterministic merges, table prewarm | [Parallelism](docs/agents/parallelism.md) |
| Machine instruction selection and scheduling, SSA, edge copies, PIC | [Machine backend](docs/agents/machine.md) |
| C frontend and canonical IR rules, layout, atomics, ABI, linking | [Frontend index](docs/agents/frontend.md) |
| Driver options, targets, assembly, link behavior | [Driver](docs/agents/driver.md) |
| Platform and backend boundaries for rendering/windowing | [Platform](docs/agents/platform.md) |

Keep this entry point compact. Put subsystem details in the matching guide,
measurements in new audit files, and task-specific progress in issues/PRs.
`CLAUDE.md` points here; do not maintain a second copy of these rules.
