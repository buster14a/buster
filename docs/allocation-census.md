# Allocation census

The optional allocation observer can emit a complete per-site request census at
normal desktop/headless application exit. It extends the same
`BUSTER_BENCH_ALLOCATIONS` recorder used by the [throughput suite](../tools/throughput/README.md).
The switch is off by default. Disabled builds retain the ordinary inline
allocation implementations without counters, TLS access or a runtime branch.

## Collect and read

Use a separate diagnostic build directory. Generation replaces that directory;
keep it separate from a normal compiler used for timings.

```sh
./build.sh generate --build-directory build-census -- -DBUSTER_BENCH_ALLOCATIONS=ON
./build.sh build --build-directory build-census --config Release -t ide
BUSTER_ALLOCATION_CENSUS=1 build-census/Release/ide cc -nostdinc -g0 -c tests/basic_c_operations.c -o build-census/basic.o 2> build-census/basic.census
python3 tools/allocation_census.py --input build-census/basic.census --output build-census/basic-report
```

`BUSTER_ALLOCATION_CENSUS=1` requests emission; it does not enable a second
recorder. The preference is captured before worker creation. Without that
request, an instrumented compiler remains quiet and still provides the existing
`allocation.arena_calls` and `allocation.arena_bytes` source-metrics keys. Those
keys keep their original calling-thread snapshot boundary, before metrics
formatting. The exit census includes later formatting and cleanup, and includes
retired OS workers, so its totals need not equal that earlier snapshot.

The offline reader validates a saved log and exports JSON and per-site CSV. It
does not launch compilers or measure throughput. Keep the log with its source,
compiler and command provenance. Use the existing throughput runner for paired
measurements, source/binary immutability checks, fresh output checks and separate
allocation replays. Never take timing/RSS evidence from the instrumented compiler.

The three-platform `Compiler throughput` harness job also compiles a separate
observer-enabled `build.c` driver and runs its small bootstrap-checker self-test.
It validates the captured exit census with this reader and retains the raw log
and JSON/CSV report. This exercises the enabled desktop reporting path on Linux,
macOS and Windows without rebuilding the full compiler. The smoke has one main
thread; worker completion remains covered by the instrumented compiler tests.

## Counter meanings

| Field | Meaning |
| --- | --- |
| `calls` | API requests, including zero-byte requests |
| `bytes` | Requested bytes; the stable source-metrics API names this `arena_bytes` |
| `padding` | Bytes advanced to satisfy arena alignment, excluding requested bytes |
| `zero_requested` | Bytes requested through the zeroed arena API |
| `zero_written` | Bytes actually cleared in the arena's dirty prefix by that API |
| `empty` | Zero-byte requests |
| `small` | Requests from 1 through 64 bytes |
| `maximum` | Largest single request; merged with maximum, never addition |
| `failures` | Failed API requests |
| `failed_bytes` | Requested bytes for failed API calls |

Arena calls are attributed to the source file, line and enclosing function of
the allocation expression. Typed calls and zeroed calls count once. Function
address or parenthesized calls remain observed through a fallback wrapper, whose
header location becomes their reported site. Names are static strings; equal
spellings in separate translation units share one textual identity.

OS reserve, commit, decommit and unreserve calls are separate kinds. Their sites
identify the OS wrapper, not the upstream caller. Their sizes are API request
sizes, not inferred native page traffic, and failures remain included in calls
and requested bytes. Never add OS totals to the logical arena byte total. The
zero/padding fields are zero for OS events. Arena failure generally terminates
the process, which yields an incomplete census that the reader rejects.

These are cumulative traffic counters. They do not describe retained/live bytes,
libc allocation, every explicit `memset`, process RSS, or physical OS zeroing.
The observer preserves the existing dirty-watermark behavior, including Darwin's
conservative treatment of discarded pages.

## Completeness and limits

The recorder uses a fixed table of 8,192 textual sites per OS thread and bounded
stack buffers for formatting. Capacity, counter overflow and lifecycle failures
terminate reporting with an error; no valid partial report is emitted. Recording
and reporting request no arena storage. Reporting retains the counters; it does
not reset an arena or the earlier cumulative snapshot.

Version 2 emits site rows, independently accumulated totals for all five event
kinds, and a footer containing the site-row count for each thread epoch. Worker
reporting runs after thread-context/pool cleanup and before the worker is marked
finished. The main report follows application cleanup and verifies that no
Buster-created worker remains live before emitting the final epoch marker. Each
thread reports once. Any later observed allocation is a lifecycle error.

A ticket lock serializes whole reports, including partial native writes, without
assuming a particular platform's pipe atomic-write size. The reader also accepts
interleaved complete epochs, reconciles every kind independently, rejects missing
or duplicate records and requires the final marker. Tab, newline, carriage-return
and backslash in site names are escaped. Addresses and row order are not IDs.

The supported completion lifecycle is the desktop/headless `buster_entry_point`
and workers created through `os_thread_create`. Mobile platform-owned entry paths,
foreign threads, abrupt termination and child-process report aggregation are not
covered. Keep each process's census log separate. A successful parser check is
structural completeness evidence within that stated lifecycle. It does not
establish compiler exit status, correct output, or output identity; keep those
checks from the throughput runner. It also cannot prove that unobserved
allocation APIs do not exist.

## Recovery provenance

This recovers the reporting portion of the historical allocation-transport patch
associated with [Optimize Compiler Allocations](https://chatgpt.com/c/6a9f2cc9-f530-83eb-b0ff-8dc0f798b69f)
(SHA-256 `9793a8a47882ff875a1966ee6f2919f73fc8ca88ef07d2a5ea337413c9753271`),
transported in commit `7b0e23ea24cc3233a91a67386122f85fdb6b1f7b`.
This verified transport is a related source variant; the conversation's original
abbreviated commit objects were unavailable.
The old `BUSTER_PROFILE_ALLOCATIONS` switch and second recorder are superseded by
the throughput observer. Version 1 lacked independent per-kind OS reconciliation;
version 2 makes that contract explicit. The old Python benchmark runner and native
launcher overlap the throughput suite and are not restored. The distinct
macro/aggregate workload generators are tracked in [#346](https://github.com/buster14a/buster/issues/346),
and page-fault/context-switch diagnostics in [#345](https://github.com/buster14a/buster/issues/345).
They require explicit corpus-capacity or result-schema/platform decisions and
are not silently added to the CI corpus.
