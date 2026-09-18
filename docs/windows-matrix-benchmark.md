# Paired Windows matrix benchmark

The `Windows paired matrix benchmark` workflow compares two immutable repository
commits using the ordinary complete Windows x86-64
`build/build.exe test_all_combinations_ci --verbose=1` target. Dispatch it with
full lowercase `baseline` and `candidate` commit SHAs. The repository variable
`GH_ACTIONS_CI_ENABLED` must be enabled, as for ordinary CI.

Three hosted `windows-2025` jobs each run both arms sequentially on the same VM,
in separate clean checkouts and build directories. Trials 1 and 3 run baseline
first; trial 2 runs candidate first. The existing native driver's four-thread
admission policy and complete configuration matrix remain responsible for work
inside each arm. There is no fixture or child-process timing instrumentation.
The branch-only pull-request trigger validates this workflow's initial #708
comparison; ordinary pull requests do not run benchmark jobs.

Each arm retains its actual checkout SHA, wall-clock start and finish, monotonic
elapsed matrix time, exit status, raw logs, and independently validated coverage
manifest. Coverage identity uses that arm's source SHA; `workflow_sha` records
the triggering workflow revision separately. CPU description, runner image,
Clang version, order, and trial number accompany the pair. The coverage report
also records the detected compiler identities and the six required rows.

A failed arm still leaves evidence and allows the other arm to run; any matrix
or coverage failure fails the pair. A successful pair additionally requires
three matching driver-module assertion inventories. This catches differences
in native execution coverage that can otherwise follow hosted CPU features.
All attempts, including failures and cancellations, belong in the final audit.
The two source trees must preserve the same semantic workload; equal assertion
counts alone do not establish that contract.

Passing this workflow means the two arms completed with matching driver
assertion counts. It is not a speedup verdict. Analyze paired full-matrix and
module durations, runner-job durations and queue delay separately; inspect
actual source changes and native classifications before accepting a result.
CPU consumption and peak simultaneous process-tree RSS are explicitly
unavailable in the uninstrumented result. Normal platform, sanitizer, mode,
throughput, and self-host checks remain separate requirements for a code PR.

## Why the original #708 samples are insufficient

[Run 35368006821](https://github.com/buster14a/buster/actions/runs/35368006821)
used separate hosted VMs for baseline `f7c4e868` and candidate `72dca950`.
All six matrices and coverage summaries passed, but their driver assertion
inventories differed. Two baseline trials recorded 34,187 / 32,360 / 32,360
assertions; baseline trial 2 and all candidate trials recorded
34,091 / 32,264 / 32,264. The candidate reported 192 executions, 44 links and
148 cache hits per invocation. The old coverage identity also used the
workflow merge SHA, so source identity must instead be recovered from the
checkout output and `source-sha.txt` for those historical artifacts.

| Trial | Baseline matrix seconds | Candidate matrix seconds |
| --- | ---: | ---: |
| 1 | 1165.501 | 1417.491 |
| 2 | 1439.599 | 1556.331 |
| 3 | 1183.856 | 1415.619 |

The unpaired medians are 1183.856 and 1417.491 seconds (+19.7%); aggregate matrix
wall time is 3788.956 versus 4389.441 seconds (+15.8%). These are observations
on differing runner workloads, not an accepted regression or speedup estimate.
They do not satisfy #708's requirement for three comparable samples per arm.
