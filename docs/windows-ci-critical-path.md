# Windows CI critical-path attribution (#2787, #2788)

The native build driver owns this bounded change. Windows AArch64 Release
uses the existing isolated unit-process runner; Windows x86-64's checked
sanitized Release uses a better-balanced projection of the same module table.
Runtime configuration rows, tests, independent references, sanitizer/check/fuzz
policy and required CI job identities stay the same.

## Recorded diagnostic baseline

The completed PR #2751 cohort is run
[37452702540](https://github.com/buster14a/buster/actions/runs/37452702540),
attempt 1. Its PR head is `966c1aef062f037b5935c5d4c6e373b91a91d66e`;
the actual merge checkout recorded in the retained native observations is
`a1e5fef1378f7bca6b81345944199566e904a361`. They are distinct identities.
Implementation baseline main is `1931cb8976e7b6bd89a062d717068c0cc8679e16`.
The archived observations justify investigation; they do not establish a
current-source speedup or a repeated timing distribution.

| Phase or module | ARM Release seconds | x64 checked sanitized Release seconds |
| --- | ---: | ---: |
| Configure | 1.152 | 2.113 |
| Build/link (not separately timed) | 96.028 | 289.457 |
| Test phase, including component dependencies | 515.245 | 704.508 |
| Post-test | 40.323 | 0.022 |
| `c_frontend_tests` | 149.893 | 517.246 |
| `compiler_driver_tests` | 307.706 | 349.105 |

ARM [job 112234748174](https://github.com/buster14a/buster/actions/runs/37452702540/job/112234748174)
has one Clang 23.1.2 AArch64 Release runtime row, unity/tests on,
sanitizer/fuzz off, four test workers and canonical table audits enabled.
Its preliminary workflow-tool and wrapper steps cost 114 and 223 seconds
separately. The wrapper run executed nine original scenarios, 22 children,
two scenario workers and the unchanged exclusive six-writer scenario.
The remaining wrapper cost is consistent with #2034's completed optimization;
this change does not recreate or weaken that harness.

x64 [job 112234748157](https://github.com/buster14a/buster/actions/runs/37452702540/job/112234748157)
has Clang 23.1.2 Release, sanitizer/checks/fuzz/tests on and table audits off.
Its original two unit processes used two workers each: the driver process
lasted 351.424 seconds, while the rest lasted 692.427 seconds.
The frontend alone accounts for 517.246 seconds of the latter.
The other modules account for approximately 530 seconds of recorded payload.
These inclusive timings support balancing the two projections; their sum is
not a measured candidate latency.

The retained artifacts are `11408667741` (ARM, ZIP SHA-256
`34ae4261b52947e615f83730111ef79a6c70d931c798e396283c4e5dcf9f3111`)
and `11409086777` (x64, ZIP SHA-256
`2f524d3e95042cb6f1a17d003292c2b288c5be1d8f70f7fd5d6950ede9b1a8d4`).
Phase summaries report CPU time and peak RSS as unknown.
Fixture-start records exist, but fixture elapsed records were not enabled;
no particular frontend or driver fixture is diagnosed here.
Runner labels do not prove CPU model, process architecture or resource limits.

## Process and coverage contract

`tools/ci_unit_tests.c` reuses exactly two child processes and the existing
persistent lane gang for concurrent pipe draining. Each child inherits the
normal test environment, with only the group and worker quota overridden.
Below four workers, or in a single-threaded build, the ordinary invocation
remains in effect. A four-worker budget yields two workers per child, preserving
OS multi-lane assertions.

The `primary` process owns the frontend module on Windows x86-64, and the
driver module on other platforms. Keeping the driver primary on ARM is
supported as a hypothesis by its recorded 308/205-second payload split; moving its frontend
to the primary process would give a less balanced 150/363-second split.
Those payloads were recorded at the serial four-worker quota; each partition
receives two workers, so the projections do not predict candidate latency.
`rest` owns every other enabled module. `primary_module` in the plan records
the exact selected anchor. Inventories retain canonical module indices,
audit ownership, and every enabled module exactly once.
Legacy group names, foreign ownership, missing/duplicate timing records,
failed/truncated captures and incomplete cleanup cannot pass.
The measurement readers remain diagnostic and never certify CI completion.

Both children share the read-only built executable and repository inputs,
while each creates its own process-qualified temporary root and local
mutable state/prewarm. The existing 90-minute child deadline, 64 MiB capture
limits, process-tree ownership and cleanup, bounded worker quotas,
deterministic replay and fail-closed aggregation are retained.
CMake replaces only `test_all`'s unit invocation; its component dependencies
remain required. Supported artifact-fanout producers retain the exact existing
configure-argument contract; Windows AArch64 has no supported fanout consumer.

## Validation and interpretation

Run the ordinary full CI on the exact candidate, including native partition
self-tests and the independent measurement/campaign negative controls.
Current-source observations, inventories, flags and actual machine facts
must be retained before using archived attribution as current validation.
See [build guidance](agents/build.md) and
[configuration ownership](ci-combination-shards.md).

The prospective sampling declaration is on
[#2787](https://github.com/buster14a/buster/issues/2787).
Screen with ordinary exact-candidate CI first. If that passes, retain three
original baseline and three original candidate CI attempts on frozen refs;
record every failed/cancelled attempt without replacement. Default runs are
uninstrumented. Differences in source obligations, toolchain, image, cache,
resource exposure or worker budgets must be reported, not pooled away.
No new numerical integration threshold is introduced.

Report phase/module/job latency separately from whole required-CI latency
and runner-seconds. In the original cohort ARM finished only 48 seconds after
x64, so a shorter ARM lane alone would expose the x64 tail.
New timing evidence belongs on the issue/PR, with source and attempt identities.

Performance validation remains **NOT VALIDATED** under
[#2761](https://github.com/buster14a/buster/issues/2761):
the approved Ryzen 7 9700X route cannot natively execute Windows AArch64 or the
Windows-specific matrix. Hosted Windows observations are diagnostics.
Linux Zen 5 timings or unrelated standalone workloads do not certify either
Windows lane. A reviewed applicable native-platform route is needed to
resolve that gap. Ordinary Windows correctness stays GitHub-hosted.

Buster first-party license selection remains unspecified, verified in
[LICENSES/README.md](../LICENSES/README.md) at the implementation baseline
and tracked in #621. No external implementation or dependency is imported.
