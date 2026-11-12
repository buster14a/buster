# all-core gauge

Run commands and resolve source paths from the repository root. Follow the
[shared agent rules](../../AGENTS.md), including the 9700X lock. [Command index](README.md).

`tools/allcore/allcore.c` is step 0 of [the all-core path](../all-core-path.md)
([#2536](https://github.com/buster14a/buster/issues/2536)): what a compile costs at
one worker and at eight on the 9700X, its cycles in user mode and in the kernel,
its peak and its faults, for the whole compile and for each stage, the stages from
each worker's own `--time-stages` report. Every later step's gate is its table. It
changes no compile: it runs the command it is given as independent processes, one
worker a CPU, the workers starting together and each worker's runs one after
another. It needs no compiler build:

```sh
cc -std=c17 -O2 -Wall -Wextra -Werror -o build/allcore tools/allcore/allcore.c
build/allcore self-test
build/allcore counted --out DIR -- COMMAND...
build/allcore sampled --out DIR -- COMMAND...
build/allcore table --out DIR --bytes 9507037
build/allcore compare --out DIR --record TABLES --record-raw RAW --bytes 9507037
```

## The parts

- **`counted`**: `--rounds` (6) rounds of a cohort of one worker (`--one`, CPU 2)
  and one of eight (`--eight`, CPUs 0 to 7: one CPU a physical core), the order
  alternating by round, `--loops` (6) compiles a worker. Each run is counted by
  counters of its own that the processes it starts inherit (the compiler's exit
  helper among them): cycles and instructions in user mode and in the kernel, and
  APERF and MPERF where the msr PMU has them. Its resource use gives the peak and
  the faults.
- **`sampled`**: `--rounds` (6) rounds of the same two cohorts (`--loops-one` 6,
  `--loops-eight` 5 compiles a worker), each under a group on every worker's CPU,
  `{cycles, cycles:u, cycles:k}`, sampled every `--period` (100,003) cycles of the
  leader with the sample's address and its time on `CLOCK_MONOTONIC`, the rows'
  clock. The tool reads the kernel's ring buffers itself (`--buffer-mb`, 16 a CPU)
  and puts each sample's deltas in the run whose span on its CPU holds its time
  and in the stage its time falls in, by that run's report. It needs
  `kernel.perf_event_paranoid` at 0 or less (the 9700X has -1) and an executable
  linked at a fixed address, as `./build.sh`'s is (`-no-pie`), whose symbols anchor
  each run's stage clock: the first sample in the parser's, the reader's and Tier
  0's functions, less the stages before each, as the record did.
- **`table`** reduces DIR to `DIR/whole.tsv` (a line a worker count: MB/s, wall
  time, both modes' cycles, user instructions, peak, faults, GHz, the page state,
  the strays) and `DIR/table.tsv` (a line a stage group: milliseconds, user and
  kernel cycles and faults, at one worker and at eight). The medians are over each
  worker's runs but its first and last; the sampled cycles a stage are means over
  the same runs, user cycles scaled to the counted median and kernel cycles less
  the sampling's own (its kernel cycles a cycle sampled times the stage's). MB/s
  is the median of the rounds' rates, each the workers' runs over their own spans:
  the record took their mean, which a round with a cold executable pulls down.
- **`compare`** puts the two tables beside a record's
  (`inflation-sq.tsv`, `stages-sq.w1.tsv` and `baseline.tsv`), cell by cell, with
  `--tolerance` (3%); `--record-raw` takes the record's MB/s as the median of its
  rounds from its raw rows, as `table` takes the gauge's.

`COMMAND`'s `%w` is the worker, `%i` the run, `%l` the cohort (`one` or `eight`) and
`%r` the round, so each worker writes its own object. Each run's standard error is
its report, kept in `DIR/PART/rROUND.COHORT/wWORKER.iRUN.txt`; the rows are the
record's runner's format, so its reducers read them too.

## On the 9700X

Each part is one hold of the lock, under 30 s: about 14 s counted and 13 s
sampled for `sqlite3.c`. Run each through `exclusive.sh` on its own, never both
in one hold:

```sh
sqlite=/var/lib/buster-9700x/corpora/preprocessor-391e729af658/sqlite
command=(./build/compiler --time-stages --lanes 1 -c -DSQLITE_OMIT_LOAD_EXTENSION -DSQLITE_THREADSAFE=0 -DSQLITE_ENABLE_MEMSYS5 -DSQLITE_ENABLE_RTREE "-I$sqlite" "$sqlite/sqlite3.c" -o "gauge/%l.w%w.o")
EXCLUSIVE_OWNER=... ~/buster-fast-bench/exclusive.sh build/allcore counted --out gauge -- "${command[@]}"
EXCLUSIVE_OWNER=... ~/buster-fast-bench/exclusive.sh build/allcore sampled --out gauge -- "${command[@]}"
build/allcore table --out gauge --bytes 9507037
```

The 9700X job's step "Gauge sqlite3.c's compile at one worker and at eight, by
stage" runs the same on every main state it measures (a push, the nightly backlog
and snapshot), never on a pull request, and puts both tables in the run's summary.
It fails when a compile fails, a counter or a sample is missing, or the objects of
one worker and of eight differ; its figures don't gate. The `Measurement storage
policy` job builds the tool with `-Werror` and runs its self-test on every run,
pull requests included.

## Traps

- **Strays.** A process the lock doesn't cover shares a worker's CPU or its core
  and slows that worker, which leaves the others a lighter host
  ([#2555](https://github.com/buster14a/buster/issues/2555)). Each cohort writes
  `rROUND.COHORT.strays.txt`: other processes that took more than half a CPU over
  it, from `/proc`'s clock ticks before and after, with the CPU each was last on.
  `whole.tsv` counts them; read the files before trusting a table that has any.
- **The page state.** A compile on a host whose runnable tasks are a quarter of
  its online CPUs takes base pages (#1561's `busy`): at one worker it takes huge
  pages, at eight base pages. `whole.tsv`'s `huge_pages` counts each state; a
  cohort that mixes them mixes two costs.
- **The peak includes the executable's file**, about 4.5 MB of the measuring
  copy's pages ([the record](https://github.com/buster14a/buster-performance/tree/222f622d5f7d46abbc333d80ec8e6ce91930fc4b/docs/measurements/all-core-rerun-2536-2026-11-12)'s
  `rss.sh`): a copy whose file is shared differently gives another peak for the
  same compile.
- **Small kernel cells.** A stage's kernel cycles are a difference: the sampled
  ones less the sampling's own, which is estimated from the counted runs' median.
  Below a few million cycles the difference is within the runs' own spread, as
  in the record.
- **The memory controllers** (DRAM bytes by stage) aren't the gauge's: the
  record's `amd_umc` counts are the whole host's, four counters a controller, and
  a fifth event multiplexes them. They stay a record's measurement.

## The gate at `4c8c3c9c`

Step 0's gate is the record's stage table
([`all-core-rerun-2536-2026-11-12`](https://github.com/buster14a/buster-performance/tree/222f622d5f7d46abbc333d80ec8e6ce91930fc4b/docs/measurements/all-core-rerun-2536-2026-11-12))
within 3%, at `4c8c3c9c`, in two holds of under 30 s. The gauge ran it twice on
12 November with the record's own executable (`8e609253…`): holds of 14.3 s and
13.4 s, then 14.9 s and 13.4 s, with no strays, no lost samples and no
multiplexed group. [Its record](https://github.com/buster14a/buster-performance/tree/359dcadc4a98855be571e0e7b64c50c44d82c9c9/docs/measurements/all-core-gauge-2536-2026-11-12)
has the raw files and both comparisons.

| within 3% of the record | run 1 | run 2 |
| --- | ---: | ---: |
| the whole compile at one worker and at eight (MB/s, wall time, both modes' cycles, user instructions, peak, faults) | 14 of 14 | 14 of 14 |
| the stage table's milliseconds, user cycles, inflation and faults at eight | 68 of 72 | 69 of 72 |
| its kernel cycles | 10 of 24 | 11 of 24 |

- **What holds:** every stage's time and user cycles at one worker and at eight,
  the inflation and the faults, and every column's total. The few misses there
  are the object's and the start's rows (under 3 ms and 6 M cycles a compile) and
  `select`'s time at eight in run 2 (2.95 ms against 2.84).
- **What doesn't:** the kernel cycles of the stages that have few. The misses
  are cells of 0.8 to 41.6 M cycles a compile; every kernel cell above that
  holds. A stage's kernel cycles are the sampled ones less the sampling's own,
  a difference within the runs' own spread: the record's two measurements of
  the same executable (the first beside #2555's stray) differ by more than 3% on
  11 of the 14 cells run 1 misses, by up to 86%, and the gauge's two runs agree
  with each other better than the record's on 10 of them.
- **The start's kernel cycles at eight** are 41.6 and 40.6 M against the
  record's 35.9: its one sampled cohort had short exit-helper teardowns (3.4 ms
  a run, the reap less the exit), where its own counted cohorts had 4.8 ms and
  the gauge's sampled ones 4.9. Six rounds of the gauge's are the steadier figure.

So the gauge reproduces the record wherever the record reproduces itself, and
a later step's gate on one stage's kernel cycles under a few tens of millions
needs a band wider than 3% or another method.
