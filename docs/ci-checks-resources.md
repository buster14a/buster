# Prospective checks resource witness

[`tools/ci_checks_resources.py`](../tools/ci_checks_resources.py) owns an
additive, external observer for #2120/#2119. It launches and stops only its own
Python observer. It never launches, signals, waits for, wraps or contains a
build/test payload. Production build policy remains in the C driver. This tool
does not establish performance acceptance or change any latency, runner-seconds,
resource, deadline, cleanup or reliability threshold.

The default is off. `BUSTER_CI_CHECKS_RESOURCES=1` additionally requires a
case-sensitive `workflow_dispatch` event, repository `buster14a/buster`, a
`github-hosted` runner environment, the matching `ci.yml` workflow ref/source,
and one of these exact prospective refs:

- `refs/heads/codex/2120-evidence-v2-combined-overlap`
- `refs/heads/codex/2120-evidence-v2-combined-all-builds`
- `refs/heads/codex/2120-evidence-v2-split-overlap`

Historical refs, ordinary CI and similarly named or differently cased refs
cannot enable this producer. Historical evidence is never rewritten.

## Interface and ownership

The caller obtains the checked-out commit, tree and `ci.yml` blob through its
existing read-only source-provenance mechanism. Pass those exact 40-character
lowercase Git object IDs, the matrix lane and the shard:

```sh
python3 -B tools/ci_checks_resources.py start \
  --session "$RUNNER_TEMP/buster-ci/checks-resources" \
  --source-revision "$SOURCE_REVISION" --source-tree "$SOURCE_TREE" \
  --workflow-blob "$WORKFLOW_BLOB" --role "$MATRIX_LANE" \
  --invocation "$MATRIX_SHARD"
```

The parent directory must exist. The absolute session directory must be new;
existing directories, traversal components, symlink ancestors and Windows
reparse ancestors are refused. POSIX sessions are created with mode 0700 and
receipts with mode 0600. Windows uses the runner directory's inherited ACLs.
POSIX operations retain a directory descriptor; Windows operations inspect the
directory and the opened file's identity. This is private trusted-workflow
storage, without a claim to defend against concurrent mutation by the same
account on Windows.

Start creates `session.json`, launches a detached observer with no inherited
stdin/stdout/stderr, then waits at most ten seconds for `ready.json`. Readiness
means that the start record, baseline CPU/memory reads and first resident-set
and OS-memory observations have been recorded; it does not certify complete
measurement coverage. The pinned
Python executable is retained as `observer_executable` in `session.json` so
workflow integration can reuse it after Windows tool setup changes PATH.
On startup failure, start terminates only its owned observer process, waits one
second, then kills that same process if needed and waits one further second.
`startup.json` retains the failure and cleanup outcome. A failed start remains
failed even if the observer previously wrote a terminal receipt.

Immediately after the unchanged combination step, use the same pinned Python:

```sh
python3 -B tools/ci_checks_resources.py stop \
  --session "$RUNNER_TEMP/buster-ci/checks-resources"
```

Stop validates the client source/run/attempt/workflow/job-key/runner environment,
publishes `stop.json`, and waits at most ten seconds for `terminal.json`.
Repeated stop is idempotent for an existing valid terminal receipt. Exit zero
requires controlled completion and usable observed or explicitly partial
measurements. Failure/startup receipts, unknown CPU endpoints, entirely unreadable
resident sets, malformed receipts and handshake timeouts fail. Stop does not
signal a PID from a receipt. The only termination path targets the `Popen`
object owned by the still-running start command.

An observer checks its independent six-hour lifetime and 108,002-sample bounds
between scans, even when no stop arrives. These are observer limits, unrelated
to payload deadlines. The client wait is bounded; an OS query that does not
return cannot be diagnosed as clean shutdown merely because that wait expires.
Cancellation or missing terminal evidence stays incomplete. Hosted job cleanup
remains responsible for the runner's end-of-job process cleanup.

Use separate start/stop workflow steps when preserving payload bodies. That
window includes the entire combination workflow step and scheduling gaps,
including bootstrap/tool setup that the step itself performs. It does not
measure checkout, earlier setup, later configure-evidence/upload steps, or other
jobs. The workflow integration and prospective review must declare that scope.

## Records and boundaries

`journal.jsonl` uses `buster-ci-checks-resources-v1` start/sample/end records.
`session.json`, `ready.json`, `stop.json`, `terminal.json` and failure receipts
share a random session identity. JSON receipts are fsynced under exclusive
staging names and published by a same-directory hard link without replacement;
pollers cannot read half-published receipts. Files must be regular, reads are
bounded, existing files are never overwritten, and malformed published JSON is
rejected. The journal is capped at 64 MiB, individual receipts at 64 KiB, and
process enumeration at 65,536 entries. Overflow prevents complete evidence.

The start record binds the declared source commit/tree/workflow blob, actual
producer SHA-256, repository, run ID, attempt, `GITHUB_JOB` key, matrix lane/shard,
workflow ref/SHA, runner environment/name/OS/architecture and image observations.
Only an explicit environment whitelist is retained; credentials, PATH, command
lines and arbitrary environment variables are not collected. Observed CPU
capacity, physical-memory capacity and OS-specific boot/namespace facts retain
unknown values when unavailable. They do not authenticate a fresh VM.

`GITHUB_JOB=test` is shared across matrix jobs. The later collector must join the
lane/shard receipt to the archived numeric GitHub job ID and runner assignment,
check immutable source/workflow/artifact bindings, and reject duplicates or
mismatches. This producer makes no GitHub API calls and invents no numeric job ID.
Labels alone do not prove VM freshness or visibility. GitHub documents fresh
VMs for standard multi-CPU hosted jobs and a shared-VM container exception for
single-CPU runners; stored environment/API/source provenance is not hardware
attestation.

## CPU and resident-set semantics

| Platform | CPU source | Resident-set source |
| --- | --- | --- |
| Linux | Aggregate `/proc/stat` counters and actual `SC_CLK_TCK` | Readable `/proc/PID/statm` resident pages times actual page size |
| Windows | `GetSystemTimes`, restricted to one processor group and at most 64 active processors | `K32EnumProcesses`, limited-query `OpenProcess`, current `K32GetProcessMemoryInfo.WorkingSetSize` |
| macOS | Per-CPU `host_processor_info(PROCESSOR_CPU_LOAD_INFO)` unsigned counters | `proc_listpids(PROC_ALL_PIDS)` and readable `proc_pidinfo(PROC_PIDTASKINFO).pti_resident_size` |

CPU is the difference between baseline and final cumulative OS counters. It
includes OS services, background activity and this observer, without exclusive
attribution to the payload. Linux execution adds user/nice/system/irq/softirq;
guest fields are already represented and are not added again. Iowait and steal
are retained separately; iowait can decrease. Windows subtracts idle from kernel
before adding user because kernel includes idle. Failed group queries and
multiple-group systems cannot claim whole-system Windows CPU coverage.

Darwin uses four unsigned 32-bit counters per processor and the XNU 10 ms tick
conversion. Wrap is handled per field, with a bounded observation window and
rejection of impossible modular jumps. CPU coverage or field changes fail.
Reported nanoseconds are integer unit conversion, without a nanosecond precision
claim. Windows/Linux wall-derived CPU capacity is diagnostic rather than an
accuracy gate: counter units do not establish accounting precision.

Resident sets are scanned at a declared **200 ms cadence**. Each scan retains
actual monotonic begin/end times, duration, gap since the previous scan start,
enumerated/read/denied/vanished/error counts, source, byte unit and observed sum.
The first gap is unknown. Slow scans naturally produce longer recorded gaps.
All readable PIDs are included, including background processes and the observer.
No process ancestry assumption determines membership. Windows PID 0 is explicitly
unreadable rather than falsely reported as vanished.

The end record reports the maximum observed **sequential resident-set scan sum**.
It is neither an exact simultaneous peak nor a guaranteed mathematical lower
bound: processes can change during the scan. Short-lived processes between
scans may be missed. Shared pages are counted repeatedly; unmapped kernel/cache
memory is outside process resident sets. Linux fast RSS accounting is itself
approximate. Windows protected processes, Darwin other-user process restrictions
and Linux procfs visibility restrictions can make coverage partial. Unknown or
unreadable usage is never represented as measured zero. Do not substitute commit
memory, sum individual historical process peaks, sum independent runner peaks,
or pool values across platforms as interchangeable measurements.

An explicit prospective no-regression review must state this scope, read/gap
limitations and uncertainty, compare the distributions, and separately review
deadlines, cleanup and reliability. These observations do not relax the existing
15% whole-CI / 10% Windows-checks latency targets or 5% runner-seconds ceiling.
There is no invented CPU/RSS growth allowance and no acceptance flag here.

## Independent OS memory state and pressure

`os_memory` is additive evidence beside CPU and the unchanged partial RSS
observations. It measures the assigned OS instance, including kernel,
background and observer activity, without identifying payload-exclusive memory.
The start record, each sample and `os_memory_final` retain actual monotonic
read begin/end/duration/gap values. Readiness includes an initial memory
observation; it does not require that observation to have full coverage. The
window includes the same complete combination step and scheduling gaps.

| Platform | Snapshot | Cumulative activity |
| --- | --- | --- |
| Linux | `/proc/meminfo` MemTotal, MemAvailable, SwapTotal and SwapFree, with explicit KiB-to-byte conversion | `/proc/pressure/memory` `some` and `full` total stall microseconds |
| Windows | `K32GetPerformanceInfo` physical total/available, commit total/limit, system cache and kernel pools, retaining raw pages and actual page size | This API supplies no invocation cumulative paging counter; activity is explicitly `not-applicable` |
| macOS | `host_statistics64(HOST_VM_INFO64)` raw free, active, inactive, wired, purgeable, speculative, compressor, throttled, file-backed, anonymous, uncompressed-in-compressor and swapped page counts; `host_page_size` supplies the actual unit | 64-bit pagein/pageout, compression/decompression and swapin/swapout counters |

Linux MemAvailable is the kernel's estimate of memory available to a new
application without swapping. It is not free RAM or process RSS. A missing or
denied PSI interface leaves the snapshot retained but activity unsupported and
the observation partial. Other read failures and malformed counters retain
errors. PSI `some` is time with at least some tasks stalled; `full` is time with
all non-idle tasks stalled. Neither is added to CPU execution. Cumulative totals
retain stall activity that may occur between snapshots.

Windows physical availability includes standby, free and zero lists. Commit
counts are distinct from resident memory: a committed page need not have been
touched. CommitLimit is a current soft limit that can change when paging files
grow. CommitPeak is explicitly **since the last system reboot**, not a peak of
this invocation; subtracting its baseline does not produce invocation peak
commit. The observer does not use the calling-process `ullAvailPageFile` value
as system-wide commit headroom.

Darwin's ABI has 160 bytes and 40 integer words. Returned revision-1 coverage
(38 words) retains the unavailable swapped-page field as unknown and marks the
observation partial; older/invalid counts are refused. Speculative pages are
already included in free pages, so they must not be added again. Gauges overlap
and must not be summed into invented available memory. The actual host page
size is queried, never assumed to be 4096 bytes. XNU can rate-limit and cache
user-facing host statistics: read intervals and the 200 ms observer cadence
do not establish 200 ms freshness of kernel snapshots.

The end record's separate `os_memory` summary reports observations, partial and
error counts, maximum actual gap, comparison errors and cumulative change.
Every successive observation and the final endpoints are checked for source,
unit, capacity and coverage drift. Memory gauges may rise or fall; cumulative
64-bit counters may not decrease. A decrease or wrap is an explicit comparison
error rather than an invented modular delta. An intermediate decrease cannot
be hidden by later endpoint recovery. Unknown, partial or erroneous memory
observations prevent the corresponding complete scoped claim.

The existing CPU/RSS `measurement_status` and stop-success contract remain
unchanged; `os_memory.status` is independent. A controlled successful stop can
therefore retain incomplete OS-memory evidence. Check both statuses and every
raw observation during resource review. Partial RSS stays partial even when
OS-memory state is observed. These distinct measurements may inform a declared
operational resource comparison; OS physical state, commit and pressure do not
replace or close an exact peak-RSS claim. Do not pool platform-specific fields,
sum independent runner peaks, infer unobserved setup/upload memory, or treat
successful protocol completion as no regression. The owning issues still need
the declared matched-sample comparison and deadline/capture/cleanup/all-attempt
review. No automatic acceptance or numerical resource allowance is added.

## Validation and primary API sources

`python3 -B tools/ci_checks_resources_test.py -v` runs focused protocol, limits,
binding and mocked Windows/macOS API controls, plus actual current-OS API and
detached start/stop smoke. Smoke identities are synthetic test fixtures and do
not qualify a cohort. Local execution covers Linux; actual Windows x64/ARM and
macOS ARM smoke must run on the hosted Release lanes before qualification. These
tests launch no C compiler or build/test payload.

- [Linux procfs semantics and visibility](https://docs.kernel.org/filesystems/proc.html)
  and [memory pressure stall accounting](https://docs.kernel.org/accounting/psi.html).
- Microsoft [GetSystemTimes](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-getsystemtimes),
  [processor groups](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-getactiveprocessorgroupcount),
  [active processor count](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-getactiveprocessorcount),
  [EnumProcesses](https://learn.microsoft.com/en-us/windows/win32/api/psapi/nf-psapi-enumprocesses),
  [OpenProcess](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-openprocess),
  [GetProcessMemoryInfo](https://learn.microsoft.com/en-us/windows/win32/api/psapi/nf-psapi-getprocessmemoryinfo)
  and [current working-set bytes](https://learn.microsoft.com/en-us/windows/win32/api/psapi/ns-psapi-process_memory_counters).
- Microsoft [GetPerformanceInfo](https://learn.microsoft.com/en-us/windows/win32/api/psapi/nf-psapi-getperformanceinfo),
  [system memory ABI/semantics](https://learn.microsoft.com/en-us/windows/win32/api/psapi/ns-psapi-performance_information)
  and [calling-process commit availability](https://learn.microsoft.com/en-us/windows/win32/api/sysinfoapi/ns-sysinfoapi-memorystatusex).
- Apple [host_processor_info](https://developer.apple.com/documentation/kernel/1502854-host_processor_info)
  and pinned XNU `f6217f891ac0bb64f3d375211650a4c1ff8ca1ea`:
  [processor counters](https://github.com/apple-oss-distributions/xnu/blob/f6217f891ac0bb64f3d375211650a4c1ff8ca1ea/osfmk/kern/processor.c),
  [tick conversion](https://github.com/apple-oss-distributions/xnu/blob/f6217f891ac0bb64f3d375211650a4c1ff8ca1ea/osfmk/kern/clock.c),
  [process ABI](https://github.com/apple-oss-distributions/xnu/blob/f6217f891ac0bb64f3d375211650a4c1ff8ca1ea/bsd/sys/proc_info.h),
  [process access checks](https://github.com/apple-oss-distributions/xnu/blob/f6217f891ac0bb64f3d375211650a4c1ff8ca1ea/bsd/kern/proc_info.c)
  and [libproc declarations](https://github.com/apple-oss-distributions/xnu/blob/f6217f891ac0bb64f3d375211650a4c1ff8ca1ea/libsyscall/wrappers/libproc/libproc.h).
- Pinned XNU [VM statistics ABI](https://github.com/apple-oss-distributions/xnu/blob/f6217f891ac0bb64f3d375211650a4c1ff8ca1ea/osfmk/mach/vm_statistics.h),
  [VM count revisions](https://github.com/apple-oss-distributions/xnu/blob/f6217f891ac0bb64f3d375211650a4c1ff8ca1ea/osfmk/mach/host_info.h),
  [host API declarations](https://github.com/apple-oss-distributions/xnu/blob/f6217f891ac0bb64f3d375211650a4c1ff8ca1ea/osfmk/mach/mach_host.defs)
  and [statistics/caching implementation](https://github.com/apple-oss-distributions/xnu/blob/f6217f891ac0bb64f3d375211650a4c1ff8ca1ea/osfmk/kern/host.c).
- GitHub [runner semantics](https://docs.github.com/en/actions/reference/runners/github-hosted-runners),
  [runner/workflow variables](https://docs.github.com/en/actions/reference/workflows-and-actions/variables)
  and [job assignment API](https://docs.github.com/en/rest/actions/workflow-jobs).

Buster first-party licensing remains unspecified/unresolved per
[`LICENSES/README.md`](../LICENSES/README.md) and #621. Apple API/source inspection
includes APSL-2.0 and component-specific BSD/historical notices; it imports no
code. This observer uses only Python's standard library and OS APIs, with no
new dependency or copied external implementation.
