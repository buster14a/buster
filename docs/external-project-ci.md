# External-project post-merge CI (#3501)

[Compatibility harnesses](agents/compatibility.md) · [CI guide](ci-github-actions.md)

Ordinary external-project integration belongs to
[external-projects.yml](../.github/workflows/external-projects.yml). It runs only
on a push to main and checks out the event's immutable `github.sha`. A queued
revision is tested even when main advances. Each run has one standard Linux,
macOS and Windows job, capped at 360 minutes, with no project matrix, reusable
caller, arbitrary dispatch, setup job or cancellation of older main pushes.
The jobs are post-merge diagnostics, outside PR/merge-group required checks.
A failed project fails its OS job; later independent projects still run.

## Assignment and migration map

The starting census is main `27aac48349c2da9c6c24bae660a0f893fa3da6d4`.
Assignments describe the harness coverage, not a passing qualification claim.

| Workload | Native assignment and compiler configuration | Previous owner |
|---|---|---|
| cJSON 1.7.19, core/utilities + independent round trip | Linux x86-64, Clang-built Release; complete existing allocator set | Capability + real-source (one full harness now) |
| zlib 1.3.1, library/archive/configure/probes | Linux x86-64, same Release compiler; existing allocator set | Capability |
| stb pinned headers, image/font/data-structure oracles | Linux x86-64, same Release compiler; existing allocator set and documented upstream exclusions | Capability |
| Lua 5.4.8 release + pinned Git suite | Linux x86-64, same Release compiler; existing production/test-interpreter and allocator coverage | Capability + real-source (one full harness now) |
| QuickJS pinned engine/compiler/runner + upstream tests | Linux x86-64, same Release compiler; no new Test262 population | Capability |
| SQLite 3.53.4 distributions, CLI/programs/database comparisons | Linux x86-64, same Release compiler; both threading configurations and existing allocator set | Capability + real-source (one full harness now) |
| cJSON/Lua/SQLite descriptor preflight, dependency/resource/runtime closures and admission receipts | Linux x86-64; distinct exact functional-admission operations retained after passing full harnesses | Real-source; indirectly apt-input reusable caller |
| Git 2.51.0 pristine Makefile build/local-operation census | Linux x86-64, same Release compiler; native Clang/GCC harness controls retained | Git qualification manual-only workflow |
| RAD pinned Linux diagnostic campaign | Linux x86-64, separate Release tree with allocations on and debug-info off; pristine controls, tests and self-host retained | RAD main-only workflow |
| FFmpeg 7.1.3 profiles | Linux x86-64 only, exact pin; retained ISO portable census followed by matched GNU17 portable and optimized/NASM censuses; independent reference/Buster records | No prior workflow owner |
| Every project above on native macOS AArch64 | Pending/unqualified; inventory only, no project execution | No existing native project workflow |
| Every project above on native Windows x86-64 | Pending/unqualified; inventory only, no project execution | No existing native project workflow |

The macOS and Windows jobs explicitly say qualification is pending. Success of
their inventory steps is not a native external-project pass. Linux outputs do
not fill those cells. No prior Linux ARM, Windows ARM or Intel Apple
external-project native workflow was found in this census; this does not reduce
the separate first-party/native-retirement architecture obligations. Routine
Apple policy remains [AArch64](apple-ci-policy.md).

The four legacy project workflows are removed, including capability's fixed
historical SQLite baseline runner. That baseline's archived results remain
historical; it cannot test a newly landed revision. The apt-input workflow keeps
its signed provisioning controls and both profiles but no longer invokes
real-source builds. Frozen support tests remain byte-identical; the existing
maintained retry test entry runs all unaffected provisioning controls and
replaces their three superseded scheduling assertions.

The Clang suite workflow remains a focused compiler inventory/preprocessing/
driver/intrinsic regression slice. It does not build LLVM or run a whole Clang
project, and stays in its pre-merge lane. Core compiler tests, sanitizers,
self-hosting, the queue completion aggregator, ruleset and retirement authority
are unchanged. Broad build/test entry points were inspected for external action
dispatch: full harnesses require explicit commands and inputs, and ordinary
`test_all` does not dispatch the portfolio.

yyjson, LZ4, sbase, DoomGeneric, musl/libc-test, CPython and the Linux kernel
frontier ([guide](agents/compatibility/linux-kernel.md)) have opt-in native
harnesses but no ordinary workflow input owner at this revision. Their
source/input/platform intake remains pending. FFmpeg's assignment covers only
the pinned Linux x86-64 ISO portable control and GNU17 portable/optimized
censuses; macOS and Windows remain pending. The unlanded userspace,
curl, PostgreSQL, OpenSSL, QEMU, Linux, MuPDF and SDL3 milestones retain
[#3176](https://github.com/buster14a/buster/issues/3176)'s evidence contracts.
Do not enable nonexistent/unqualified input recipes or introduce a child
workflow per project. Their owners must add a pinned recipe to this shared job
and native assignment table when ready.

## Results and evidence

The native `build external_projects PROJECT` entry binds execution to the
original main push, verifies actual Git HEAD, routes one fixed project and
compiler directory, and persists started/completed status with project,
configuration, SHA, run/attempt and diagnostic duration. It does not reinterpret
harness failure as a pass or replace child deadlines. Per-project harness logs
retain configure/compile/link/runtime/test attribution, commands, reference
results, provenance, test denominators and input controls. Git/RAD keep their
existing child watchdogs. The functional admission path still requires actual
passing oracle evidence and never waives Lua/SQLite failures.

Source/machine/compiler evidence is uploaded early. Each completed project has
its own immediate upload before the next project; an OS summary and final
evidence upload supplement those checkpoints. A hard job timeout can prevent the
current project's upload: inspect the retained Actions log, last started record,
earlier project artifacts and initial assignment. Never call absent completion
evidence a pass. Workflow outcomes `skipped` mean not-run, not unsupported green;
the macOS/Windows summaries explicitly record pending/unqualified. Preserve
failure, cancellation, timed-out and incomplete records. Hosted durations are
scheduling diagnostics, not approved compiler-performance measurements.

Pins and component license files remain with project evidence. cJSON retains
its Unity/JSON-patch fixture terms; stb retains its dual-license document; Lua
retains production and test-suite headers; SQLite retains its source and
autosetup license files; Git retains COPYING plus sha1dc/reftable/Clar/xdiff and
host-dependency terms. RAD's pristine harness records its upstream license.
FFmpeg records the pinned upstream `LICENSE.md`; the workflow keeps its
default LGPL configuration by disabling GPL, nonfree and version-3 components.
It does not upload the FFmpeg checkout, generated objects or binaries. This
scheduling change imports no upstream implementation and does not grant a
Buster first-party license; that decision remains unselected under
[LICENSES/README.md](../LICENSES/README.md) and #621.

## FFmpeg cost and acceptance limits

The existing Linux x86-64 job stays at its 360-minute limit and keeps every
current project stage before FFmpeg. For each portable profile, four stages on
each of two sides
have 900-second child deadlines, and its probes and preparation add at most 660
seconds, for a 131-minute command ceiling. The pristine-input checks run before
and after the harness; each runs six Git child commands with 600-second
deadlines, adding 120 minutes. Each portable profile's configured child-deadline
sum is 251 minutes. The optimized profile adds 30 minutes of checkasm build
limits, 2 minutes of checkasm invocation limits and 2 minutes of NASM probes,
making 285 minutes. The three profiles sum to 787 minutes of child limits;
nested per-action deadlines are contained within their parent stages.
It sums limits, not observed runtime. Spawn and cleanup work, file operations,
checkout/verification, and artifact upload are outside that sum and use the
same job budget. This ceiling does not establish headroom after earlier
portfolio stages. No existing watchdog or job deadline is raised. Since FFmpeg
runs last, earlier project stages keep their current order; if the shared job
reaches its fixed deadline first, the FFmpeg receipt remains incomplete and the
portfolio must fail.

The FFmpeg route preserves the original ISO portable action before attempting
the separately named GNU17 portable and optimized profiles. It installs an
upstream-compatible external NASM; the harness records its version and SHA-256.
Both GNU profiles use upstream `--stdc=gnu17` on both compilers, preserving
the ISO diagnostic contract and the unchanged-action gate owned by #4296.
`build/ffmpeg-profiles.tsv` publishes not-run and started/incomplete records
before execution and final per-profile pass/fail records after it. A failed
earlier profile does not suppress later diagnostics; all three must pass the
portfolio result. No sampled media, performance experiment or unsupported ISA
is requested. These are scheduled diagnostic recipes, not completed optimized
qualification. The first actual main-push run must supply measured cost and
artifact-size evidence; the fixed cap is not a headroom guarantee.

Run 38428249840 completed its listed Linux project stages in 31 minutes 49
seconds and retained a 3,290,671,945-byte artifact, but its later functional
admission failed. It is useful historical scheduling context, not a green
end-to-end portfolio receipt or a guaranteed headroom measurement. The workflow
does not claim that this one run bounds future variation or artifact growth.
Only text evidence, selected generated configuration files and captured logs
are added to the immediate FFmpeg artifact; upstream source, objects and
binaries remain excluded.

The portable collector requires both pinned H.264 translation units to have a
successful Buster `cc` action, nonempty output object, complete captured
argv/stdout/stderr/status, clean capture/process cleanup and observed CPU and
memory measurements. It also retains the full reference-versus-candidate
configuration, build, media, no-sample FATE and pristine-after checks.
Unrelated failures in the existing census continue to fail the FFmpeg result.
This portfolio run supplies functional/resource diagnostics only; it is not a
performance qualification. The original #4050 acceptance stays open until a
main-only run records the requested pristine translation-unit completion and
time/RSS evidence. Broader #3177/#4078 qualification remains separate.

## Scheduling policy and future changes

`tools/external_project_policy.c` runs in the existing workflow lint jobs.
It checks the complete workflow directory, sole owner, exact event-SHA checkout,
three standard OS jobs, no fan-out/call/dispatch bypass, 360-minute caps and
main-burst retention. Mutation fixtures reject altered events, tags, branches,
SHA binding, budget, runner, cancellation, duplicate/missing assignment and a
fourth reusable job. FFmpeg controls also bind the exact upstream pin, Linux
owner route and text-only artifact allowlist, rejecting a broad build-directory
upload. Native event fixtures separately cover main advancing while a queued
original SHA is tested. The scanner admits a restricted canonical
owner syntax, not general YAML; topology changes must update its fixtures.
The expanded FFmpeg allowlist retains only text action/configuration/checkasm
evidence from all three profiles, including independent-Clang and external-NASM
actions. Mutation controls reject broad GNU build-directory/object uploads,
missing NASM installation and GNU full-project commands in nonowner workflows.

Extra OS jobs require retained evidence of an actual 360-minute hosted timeout:
run/attempt, SHA, machine/image, complete assignment, per-project phase
durations, finished/unfinished work and diagnosis separating valid aggregate
work from a stuck/regressed child. A legacy 60/90-minute timeout or forecast is
insufficient. Use the minimum additional disjoint assignments whose union
preserves coverage; do not repeat a failing project on another runner. Never
raise inner watchdogs or schedule a six-hour synthetic run to justify shards.
Separately authorized benchmark/acceptance experiments keep their own contracts
and approved Zen 5 execution route.

Deployment acceptance requires a real main-push execution and inspection of
all three native OS inventories, Linux project evidence and exact identities.
Source-policy tests alone do not establish deployed project qualification.
