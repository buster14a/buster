# microarch

Run commands and resolve source paths from the repository root. Follow the
[shared agent rules](../../AGENTS.md), including the 9700X lock and `cores`
requirements. [Command index](README.md).

## Micro-architecture campaign

`tools/microarch/` measures the compiler on Zen 4 and Zen 5 hosts and turns the
measurements into tables. It records whole-process times and `--time-stages`
stages, counter groups that perf never multiplexes, every core event and metric
`perf list` names, and per-function profiles: leader-sampled top-down, memory,
frontend and microcode groups, IBS ops and LBR. It also records lane and
multi-process scaling, energy and, where the host exposes `amd_umc`, DRAM
traffic. It changes no host setting and needs no root. It needs
`perf_event_paranoid` low enough for per-thread counting and kernel samples
(−1 on both measured hosts), and the `ibs_op` and `ibs_fetch` PMUs and LBR
(`-j any,u`).

- `uarch.c` is the runner and reducer, in C. `time`, `stat`, `sweep`, `metrics`
  and `fanout` run processes and write raw files. `summarize`, `stages`, `symbols`,
  `branches`, `memory` and `topdown` only read them, so every table can be rebuilt
  from a record. `check` reads a sweep's or a metrics run's records for a range of
  entries without running anything. `generate-generic N` writes #1341's `_Generic` unit, and `test`
  checks every parser against fixtures of perf's and the compiler's real output,
  and the resume and failure rules below.
  - **Resuming.** Every run writes its completion record after the run ends,
    through a rename:
    - `stat`, `sweep` and `metrics` write a `.status` file;
    - `time` writes a `logs/LABEL.rN.time` row;
    - `fanout` writes a `logs/LABEL.rN.fanout` row.

    `time.tsv` and `fanout.tsv` are rebuilt from those rows. A rerun skips the
    runs that have a record and runs again those that don't (an interrupted run
    leaves its log but no record), so each run gives exactly one row.
  - **Failure.** A command fails when a run failed, now or in an earlier
    invocation. In `sweep` and `metrics`, perf's refusal of an event or metric it
    can't open (nonzero status, no counted line) is a result their tables record,
    not a failure; a counted run with a nonzero status is a failure.
  - **Chunks.** `run.sh` asks `uarch check` about every run of a sweep or metrics
    chunk before running it. A complete chunk is skipped. A chunk with a recorded
    failure fails the step again on every rerun. `RETRY_FAILED=1` removes the
    failed runs' records first, so that they run again.
- `run.sh` holds the steps (listed in its header). Each step, or each chunk of a
  long one, takes the host's lock or core grant itself, for that chunk alone.
  Every step returns its real status; the first step that fails stops the run
  (`KEEP_GOING=1` runs the rest) and `run.sh` exits nonzero.
  - **Measurements.** A system-wide perf measurement (package energy, APERF/MPERF,
    UMC) is one unit with the command it measures: its CSV counts only once
    `CSV.done` is written after both succeed. One that is unfinished runs again
    whole. Any fanout batches it measured lose their completion records first, so
    that they run inside the new measurement instead of being skipped under it.
- `hosts/zen4-7940hs.sh` and `hosts/zen5-9700x.sh` hold each host's hold
  functions, CPUs, dispatch width, the event names that differ between
  generations, and chunk sizes. A new host needs a profile like these.
- `workloads.sh` holds the workloads, as command lines relative to the run root:
  - sqlite3.c with the CI's options: `sq-c` (`-c --lanes 1`), `sq-syn` and `sq-E`;
  - the compiler's own `main.c` (`self-syn`, a `.i`);
  - musl's 1,355 sources on N lanes (`musl-syn`);
  - tcc as the reference compiler (`tcc-c`).
- `tables.sh` writes the report's tables from one or more hosts' results.
  `inline-split.sh` splits IBS samples by inlined function or by line.
  `pack.sh` copies a host's results into a buster-performance record.

Check the tool and the driver before a campaign. `test.sh` runs `run.sh` on fake
compilers, a fake perf, a fake tcc and a two-commit fake checkout. It checks:

- the driver's exit status and `KEEP_GOING`;
- `validate`'s comparisons and fresh outputs;
- that a profile whose perf record or reducer fails is never marked done;
- that sweep and metrics chunks report recorded failures, and `RETRY_FAILED`;
- that a finished `uncore` measurement is kept and an unfinished one is measured
  again with its batches;
- the commits `build-commit` and `setup` reuse.

```sh
mkdir -p build/microarch
clang -std=c17 -O2 -Wall -Wextra -Werror -o build/microarch/uarch tools/microarch/uarch.c -lm
build/microarch/uarch test
tools/microarch/test.sh
```

### Running

The run root is `build/microarch` unless `ROOT` names another directory. `setup`
prepares it:

- links the frozen corpus (`CORPUS`, the 9700X's copy, or `tools/ci/fetch-corpus.sh`);
- builds this checkout (`REPOSITORY`, this one by default) for each of
  `BUILD_ARCHES` (default `znver4 znver5 x86-64`) through the host's build hold,
  into `bin/compiler-ARCH`. `bin/compiler-ARCH.commit` records the commit, with
  `-dirty` when tracked files differ from it, and an existing build is reused only
  when that file names the checkout's commit;
- writes `predefines-znver5.h` and `self-main.i`, the compiler's `main.c`
  preprocessed by `bin/compiler-znver4`.

`MICROARCH_TITLE` and `MICROARCH_LINK` (and `MICROARCH_OWNER` on the 9700X) name
the session in `cores status` and the lock's owner file.

```sh
MICROARCH_TITLE=... MICROARCH_LINK=... tools/microarch/run.sh tools/microarch/hosts/zen4-7940hs.sh \
    setup env validate time groups profiles energy scaling sweep metrics time-self groups-self summarize
```

To run identical executables and input everywhere, build once and copy the run
root's `bin/` and `self-main.i` to the other host before its `setup` (which
then builds nothing). On the 9700X, start the run detached from a clone in
`~/buster-fast-bench/<task>-<agent>/`, and stop it by its PID file, never by a
pattern ([benchpress](benchpress.md)):

```sh
(setsid nohup env ROOT=$PWD/build/microarch MICROARCH_LINK=... tools/microarch/run.sh tools/microarch/hosts/zen5-9700x.sh \
    setup env validate time groups profiles energy scaling uncore sweep metrics summarize > microarch.log 2>&1 < /dev/null &
    echo $! > microarch.pid)
```

The 9700X profile holds the lock about a minute per chunk and pauses 30 s
between holds (`PAUSE`). The whole list is about 40 holds there, plus waits for
CI; on the 7940HS about 25 minutes. `SWEEP_WORKLOADS`, `METRIC_WORKLOADS` and
the profile's `VARIANTS`, `SWEEP_VARIANTS` and `SELF_VARIANTS` narrow a run.
Then:

```sh
tools/microarch/tables.sh build/microarch/results/zen4-7940hs:6 build/microarch/results/zen5-9700x:8 > tables.md
```

### Steps

| step | what | runs |
| --- | --- | --- |
| `validate` | each build's object, `-E` output, default-lanes object and diagnostics, from fresh outputs; a failed command stops the step, and outputs that differ between builds fail it (diagnostics only warn with `VALIDATE_DIAGNOSTICS=warn`, for #1339) | 1 |
| `time`, `time-self` | whole processes (wall, rusage), with `--time-stages` runs | 11 / 3 rotated rounds |
| `groups`, `groups-self` | six perf stat groups of at most five counters: user and kernel cycles and instructions; top-down level 1; level 2 (with the frontend stall at `cmask` = width and microcoded ops); branches; frontend; memory | 5 / 2 per build and workload |
| `sweep` | every core event, user and kernel, each in one group with `cycles:u` and `instructions:u`, with a one-event fallback | 2 |
| `metrics` | every perf metric, one `perf stat -M` each | 2 |
| `profiles`, `profiles-same` | leader-sampled groups (`{...}:S`, 50,021-cycle period) reduced per function; IBS ops with data source and latency, all and L3 misses only; LBR mispredicts by site; cycles with the kernel; musl's cycles; the microcode group. A profile is done (`NAME.done`, written last) only when perf record and its reducers succeeded with rows; a failed one is removed and recorded again on the next run | 8 compiles each |
| `profile-self` | IBS ops of `self-syn`, by function and by `check_common_evaluated`'s lines | 1 |
| `scaling` | `sq-c` and `musl-syn` on 1..N lanes; K independent compiles on K cores with package energy | 5 / 2 / 4 rounds |
| `energy` | package and per-core energy, APERF/MPERF | 10 compiles per build |
| `uncore` | UMC CAS commands and activates | 8 compiles; 2 batches of 8 |
| `ab`, `ab-self` | `AB_BUILDS` against each other on `sq-c`/`sq-syn`, or on `self-syn`, with the stage clock and counters, into `AB_NAME` | 11 / 3 rotated rounds |
| `generic-scaling` | `AB_BUILDS` on generated units of 250 to 4,000 `_Generic` selections | 3 per size |

`build-commit NAME COMMIT ARCH[,ARCH...]` builds `bin/compiler-NAME-ARCH` from
`git archive COMMIT` under the build hold, for the A/B steps. It resolves `COMMIT`
to its full id first. It reuses a tree (`trees/NAME/COMMIT`) or a build
(`bin/compiler-NAME-ARCH.commit`) only when it holds that commit; a name that
holds another commit fails the step, so an A/B can't measure a stale build under a
new commit's name:

```sh
tools/microarch/run.sh tools/microarch/hosts/zen5-9700x.sh build-commit pre1049 aca9e53a znver5 build-commit m1049 e36740b8 znver5
AB_NAME=ab3 AB_BUILDS="pre1049-znver5 m1049-znver5 znver5" tools/microarch/run.sh tools/microarch/hosts/zen5-9700x.sh ab
```

### Records

Raw results go to buster-performance, never to this repository's
`docs/measurements` (AGENTS.md, "Issue ownership"). `pack.sh` copies a host's
summaries into `docs/measurements/TOPIC-DATE/HOST` and the per-run files into
its `raw.tar.zst`. It strips device identifiers from the kernel command line and
refuses a record that carries another session's lock lines. Name this
repository's commit of the harness in the record; `env/settings.txt` has it as
`harness_commit`. Large files (perf.data, executables with their license files)
go to a release of the record.

The first campaign, at main `9e7f918f` on 4 November 2026, is
[buster-performance#253](https://github.com/buster14a/buster-performance/pull/253)
with the release [`microarchitecture-two-hosts-2026-11-04`](https://github.com/buster14a/buster-performance/releases/tag/microarchitecture-two-hosts-2026-11-04).
It found [#1339](https://github.com/buster14a/buster/issues/1339),
[#1340](https://github.com/buster14a/buster/issues/1340) and
[#1341](https://github.com/buster14a/buster/issues/1341).

### Traps

- **Read perf's group columns by name.** A leader-sampled group's members print
  in a host-dependent order in `perf script -F period,event,ip,sym`; `symbols`
  and `topdown` read columns by event name.
- **CSV commas and locales.** `perf stat -x,` prints an event with terms
  (`cpu/EVENT,cmask=0x8/u`) with its commas, and a locale with comma decimals
  prints duty as `100,00`. `csv_split` rejoins the event, and every run is in
  `LC_ALL=C`.
- **The sample-rate cap.** The 9700X caps perf at `perf_event_max_sample_rate`
  63,300/s. A 50,021-cycle period at 5.5 GHz keeps about 69% of the cycles; the
  per-function shares are ratios within the samples.
- **Event semantics change between generations.**
  - Zen 5's `ic_tag_hit_miss.*` counts about 24 times Zen 4's accesses for the
    same code; compare `ic_cache_fill_l2` and `op_cache_hit_miss` instead.
  - Zen 5's `l2_cache_req_stat.ic_dc_miss_in_l2` reads below its DRAM fills in
    some functions; use `ls_dmnd_fills_from_sys.*`.
- **IBS and listed events.** IBS needs `swfilt=1` to take `/u`; `-d -W` adds the
  data source and weight per thread. `perf list --raw-dump pmu` also names tool
  events, sensors (`temp1`) and amd_l3 events (`l3_lookup_state.*`) that can't
  count per thread; `env` drops the tool events, and the sensors and L3 events
  stay in a sweep with no value.
- **Leader sampling blends functions.** Each sample's deltas go to the leader's
  address, so a function much shorter than the period shows its caller's ratios.
  Use IBS for exact per-op attribution.
