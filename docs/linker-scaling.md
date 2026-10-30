# Linker lanes and filesystem writes

This records the frozen 9700X measurements for [#87](https://github.com/buster14a/buster/issues/87),
taken on 2026-10-23, and the complete foreign links' of 2026-10-24. It adds
independently controlled filesystem factors and bounded runs to
`benchmark-floors`; it does not change the production writer.

The large direct pipeline reaches a broad plateau around 3–4 physical lanes
on this host. Adding its available SMT siblings does not improve the total.
Foreign exchange stops helping earlier than resolution. The complete foreign
link, its conversion on lanes too, is fastest on 7 lanes, twice as fast as on
one, and every phase of it has a measured limit ("Complete foreign links"):
the few still fastest at the machine's 15 online logical CPUs are bounded by
them, past which lanes only lose, and `link-foreign` runs at most a lane per
CPU. Production retains pwrite after the filesystem comparisons below.

## Frozen inputs and host

- Compiler baseline: `fc45e674d13d9257d66c02b05d78adf85ddae7ea`, an unmodified
  tracked-source archive. Compiler SHA-256:
  `041461f2bb8d086d00adbe0c732b59c9eb3666d3dce202c13238ac5f58f8a8e1`.
- Floors source: baseline plus
  `03d97003b098450b39c7e5de799bcb9bb8f0b7cd` and
  `7a6f47bdf109262d706f688827bc16e5f6086252`.
  Its compiler SHA-256:
  `6466059814297a2ae6452027143a04241c0407911c15c8d8b15ccc0dfe32a35f`.
- `david@benchpress`, Ryzen 7 9700X, Linux `6.18.50-2-lts`,
  Clang `23.1.1`. All binaries use the frozen source's `./build.sh` defaults:
  `-O3 -g -march=native -fwrapv -funsigned-char -fno-strict-aliasing -Wall -Werror`.
- CPU 2 used the performance governor; all other online CPUs used powersave.
  CPU 10, CPU 2's SMT sibling, was offline. These settings were observed,
  not changed for the experiment.
- The original 1–8 sweep uses prefixes of CPUs `2,0,1,3,4,5,6,7`: one physical
  core per lane, CPU 2 for one-lane timing. The later logical-CPU extension
  appends the online SMT siblings, as recorded below. Other core governors
  and natural barriers are part of this measured configuration; the numbers
  do not describe eight cores held artificially at a fixed clock.
- tmpfs output: `/tmp/linker-scaling87-output`.
  btrfs output: `~/buster-fast-bench/linker-scaling87/fs-output`, under
  `/home`, mounted with
  `rw,noatime,compress=zstd:3,ssd,space_cache=v2,subvol=/@home`.
- Every build, test and measurement acquired the shared lock separately,
  with `EXCLUSIVE_OWNER=codex-linker-scaling87`, a descriptive
  `EXCLUSIVE_TITLE`, and `EXCLUSIVE_LINK=https://github.com/buster14a/buster/issues/87`.
  CI retained priority. The first complete btrfs floor matrix held the lock
  for about three minutes; the added filters bound subsequent runs.

Raw evidence is retained on that host in
`~/buster-fast-bench/linker-scaling87/logs/`; immutable source/binary
directories are `base/`, `floors/`, `mmap/`, and `mmap-reserved/`
under the same parent. The local evidence copy is
`/home/david/Documents/Codex/2026-10-23/ta/work/scaling87-evidence/`.
Its `README.txt` records provenance; `phase-summary.json`,
`logical-phase-summary.json`, `floors64-summary.json`, and
`floors4-summary.json` retain the parsed process medians. These are retained
experiment artifacts, not a hosted CI
result or measurements of later main revisions.

## Lane measurements

Both phase sweeps use seed 87, eleven inner samples after warm-up, and three
process-level outer repetitions. Lane orders alternate:
`1,2,3,4,5,6,7,8`, `8,7,6,5,4,3,2,1`, and
`4,1,8,2,7,3,6,5`. Each direct invocation has its own lock; each foreign
sweep has its own lock.

The direct command, run in the frozen baseline directory, is:

```sh
EXCLUSIVE_OWNER=codex-linker-scaling87 \
EXCLUSIVE_TITLE="Linker #87 direct phases" \
EXCLUSIVE_LINK=https://github.com/buster14a/buster/issues/87 \
~/buster-fast-bench/exclusive.sh ./build/compiler benchmark-link \
  --only direct --cpus 2,0,1,3 --seed 87 --runs 11 \
  --cache ../snapshot-cache --directory /tmp/linker-scaling87-output
```

Change `--cpus` to the required prefix for each lane count. Do not add
`taskset`: the benchmark pins its own lanes. Its default inputs are one
worker and 2,000 workers (up to eight filler instructions), and 102,400
workers in 512 units (up to 64 filler instructions). Snapshot construction
is outside timing. Every written program runs against its model; repeated
writes must preserve the warm-up build ID. Logs are
`direct-tmpfs-lN-rR.log`.

The foreign command uses the same seed, input sizes and CPU order:

```sh
EXCLUSIVE_OWNER=codex-linker-scaling87 \
EXCLUSIVE_TITLE="Linker #87 foreign phases" \
EXCLUSIVE_LINK=https://github.com/buster14a/buster/issues/87 \
~/buster-fast-bench/exclusive.sh ./build/compiler benchmark-foreign \
  --lanes 1,2,3,4,5,6,7,8 --cpus 2,0,1,3,4,5,6,7 \
  --seed 87 --runs 11 --directory /tmp/linker-scaling87-output
```

Both vector and exact paths ran for dynamic, static and archive-member input;
one unit has no archive-member case. Logs are `foreign-sweepR.log`.

### Direct linking and foreign resolution results

Each cell is the median of three process medians, in microseconds, for the
102,400-worker input. The direct total is the sum of independently measured
part medians, as `benchmark-link` defines it, rather than a single timed
wall-clock interval.

The foreign rows measure only `foreign_run`: input opening and parsing,
symbol resolution, archive extraction and COMDAT selection. They exclude
`ForeignLink` conversion, relocation application and executable output
writing. The frozen `fc45e67` baseline predates that output bridge, so these
figures are resolution costs, not complete foreign-input-to-executable
times. Their timer includes each sample's lane thread creation and join,
but excludes `foreign_release`. The later persistent-worker implementation
and `benchmark-foreign-output` have different timing scopes; neither is
represented by these frozen measurements.

| Direct kind | 1 lane | 2 | 3 | 4 | 5 | 6 | 7 | 8 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| dynamic PIE | 7219.1 | 4789.1 | 4427.8 | 4358.6 | 4367.2 | 4579.4 | 4351.9 | 4438.7 |
| static executable | 8085.9 | 5646.3 | 5269.1 | 5127.9 | 5253.4 | 5200.6 | 5236.4 | 5249.3 |
| static PIE | 8236.7 | 5731.3 | 5355.9 | 5190.9 | 5242.9 | 5272.6 | 5259.6 | 5298.0 |
| shared library | 15166.9 | 12759.8 | 12631.4 | 12287.5 | 12747.2 | 12538.6 | 13095.2 | 12673.2 |

Nearby direct minima do not establish a universal dispatch threshold.
The shared-library output includes temporary
file publication and has a different finish cost from an executable written
in place. Small dynamic/shared programs favor one lane; all smaller-case
samples are retained in the evidence.

| Foreign vector kind | 1 lane | 2 | 3 | 4 | 5 | 6 | 7 | 8 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| dynamic | 14086.7 | 9064.8 | 7226.7 | 6578.3 | 6599.7 | 6670.5 | 6933.0 | 7196.0 |
| static | 14741.1 | 9372.4 | 7642.8 | 6840.0 | 6801.5 | 6771.6 | 6990.2 | 7538.9 |
| members | 18730.8 | 11342.3 | 9500.2 | 8764.0 | 8311.0 | 8528.9 | 8199.2 | 8765.4 |

| Foreign exact kind | 1 lane | 2 | 3 | 4 | 5 | 6 | 7 | 8 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| dynamic | 17012.4 | 10381.1 | 8158.1 | 7156.2 | 6870.0 | 6803.0 | 7065.7 | 8027.0 |
| static | 17849.4 | 10825.6 | 8726.9 | 7769.8 | 7260.9 | 7192.1 | 7398.1 | 7815.0 |
| members | 24460.4 | 15969.3 | 13552.0 | 12474.6 | 12038.4 | 12227.2 | 12360.0 | 11978.2 |

### Observed phase minima

Each cell gives the lane count with the lowest median of the three process
medians, followed by that time in microseconds. These are observations on the
large input, not statistically established thresholds. A minimum at eight
is limited by the available physical cores. Small differences in cheap phases
are not evidence that a particular lane count should be selected.

Writer phase times include the phase barriers before their timestamps. The
finish timestamp precedes the final status broadcast, so that broadcast is
outside the reported finish phase.

| Writer phase | Dynamic PIE | Static executable | Static PIE | Shared library |
| --- | ---: | ---: | ---: | ---: |
| count | 5 (268.1) | 6 (236.3) | 6 (278.5) | 8 (844.6) |
| layout | 5 (48.1) | 4 (62.2) | 5 (59.9) | 4 (296.0) |
| symbols | 8 (15.2) | 8 (21.9) | 7 (22.3) | 8 (76.2) |
| dynamic | 8 (22.0) | 8 (21.3) | 8 (25.0) | 8 (480.7) |
| open | 1 (15.7) | 1 (353.3) | 2 (347.0) | 8 (32.9) |
| chunks | 4 (3760.1) | 4 (3875.5) | 4 (3873.6) | 2 (7389.9) |
| finish | 1 (2.4) | 1 (2.7) | 3 (2.7) | 1 (1443.9) |

Foreign phase times are the slowest lane's cumulative time, not additive
wall-clock slices. In particular, exchange includes counts, prefix placement,
fill and its internal count barrier, but excludes its final exchange barrier.

| Foreign vector phase | Dynamic | Static | Members |
| --- | ---: | ---: | ---: |
| open | 6 (643.1) | 7 (527.5) | 7 (41.7) |
| indices | 1 (82.7) | 1 (123.9) | 2 (832.7) |
| headers | 8 (88.4) | 8 (129.6) | 8 (168.1) |
| symbols | 5 (1731.3) | 5 (1623.0) | 4 (2101.7) |
| exchange | 4 (878.6) | 3 (1019.4) | 2 (866.7) |
| resolve | 6 (1316.3) | 6 (1227.1) | 8 (1907.2) |
| load | 6 (2.3) | 1 (12.6) | 4 (65.3) |
| groups | 6 (45.5) | 8 (85.8) | 8 (75.5) |
| winners | 4 (1024.7) | 4 (923.3) | 3 (921.3) |

Exchange medians at 1 / best / 8 lanes are 1442.7 / 878.6 / 1221.4 us
for dynamic input, 1571.8 / 1019.4 / 1382.4 us for static input, and
1143.5 / 866.7 / 1292.9 us for members. Resolution scales farther: its
corresponding times are 4935.1 / 1316.3 / 1423.4, 5184.9 / 1227.1 / 1457.6,
and 9208.3 / 1907.2 / 1907.2 us. That is a reason to measure producer,
exchange and consumer separately while retaining the foreign-resolution total;
it is not evidence that exchange can be removed.

## Online SMT extension

The later sweep reuses the same frozen compiler, seed, snapshot cache and
default input shapes. It measures 8, 10, 12 and 15 lanes on prefixes of
`2,0,1,3,4,5,6,7,8,9,11,12,13,14,15`. The first eight lanes use separate
physical cores; the remaining lanes share cores 0, 1, 3, 4, 5, 6 and 7.
CPU 10 remains offline, and no governors or host settings were changed.
This tests SMT on eight physical cores, not scaling beyond eight cores.

The three process-level repetition orders are `8,10,12,15`,
`15,12,10,8`, and `12,8,15,10`, with eleven inner samples after warm-up.
Use the direct command above with each CPU prefix, and the foreign command
with the full CPU list and the corresponding `--lanes` order. Each direct
invocation and each foreign sweep holds its own shared lock. The initial
eight-lane control was submitted around 10:42 UTC and completed by 11:39;
coordinated measurement windows and CI priority then delayed the remaining
first-round direct commands until 12:38 and 13:56–13:58. The later direct
rounds completed around 14:00–14:03. All controls and scheduling gaps are
retained; no sample was discarded. Compare with this sweep's fresh
eight-lane controls, rather than only the earlier eight-lane numbers.

Logs are `logical-direct-lN-rR.log` and `logical-foreign-rR.log`.
`logical-cpu-protocol.txt` records the plan and scheduling;
`logical-phase-summary.json` retains every shape, kind, path and phase.
The following cells give the median and [minimum, maximum] of the three
process medians, in microseconds, for 102,400 workers. Timing scopes remain
as qualified above, including the frozen foreign thread lifecycle cost.

| Direct kind | 8 lanes | 10 | 12 | 15 |
| --- | ---: | ---: | ---: | ---: |
| dynamic PIE | 4393.5 [4323.3, 4446.5] | 4903.9 [4851.6, 5108.3] | 5289.3 [5287.9, 5471.1] | 5534.6 [5502.9, 5575.0] |
| static executable | 5219.2 [5210.3, 5288.5] | 5752.4 [5574.2, 5761.3] | 6149.9 [6080.1, 6196.8] | 6420.3 [6330.5, 6427.5] |
| static PIE | 5462.3 [5330.2, 5501.4] | 5751.6 [5651.0, 5791.6] | 6235.3 [6184.0, 6309.2] | 6489.4 [6449.2, 6534.8] |
| shared library | 12187.9 [12140.8, 12660.2] | 12914.1 [12621.5, 13272.4] | 13765.4 [13422.3, 14081.2] | 14254.6 [14072.5, 14635.0] |

Every large direct total is slower above eight lanes in each paired round.
The shared-library ten-lane range overlaps the eight-lane range; all other
extra-lane ranges lie wholly above their controls. SMT does not provide a
total-time win in this measured configuration.

| Foreign resolution | 8 lanes | 10 | 12 | 15 |
| --- | ---: | ---: | ---: | ---: |
| vector dynamic | 7270.2 [7225.9, 7367.2] | 8698.8 [8543.8, 8747.3] | 8966.5 [8831.9, 8990.0] | 10947.5 [10941.1, 11435.1] |
| vector static | 7565.7 [7562.5, 7570.7] | 8615.1 [8548.3, 8699.2] | 8914.1 [8902.8, 8995.4] | 11323.5 [10995.0, 11343.1] |
| vector members | 8663.1 [8574.5, 8761.0] | 9892.9 [9780.5, 9895.1] | 10934.4 [10702.0, 10973.0] | 12771.6 [11846.4, 12932.3] |
| exact dynamic | 7444.4 [7391.9, 7521.9] | 8837.6 [8748.7, 9018.9] | 8968.4 [8903.8, 9557.1] | 11327.2 [11247.4, 11532.5] |
| exact static | 7904.2 [7802.4, 7978.5] | 8999.5 [8932.8, 9115.4] | 9690.0 [9384.7, 10114.0] | 11408.5 [11070.6, 11881.7] |
| exact members | 12109.5 [11841.0, 12916.5] | 13204.6 [13091.5, 13284.0] | 14343.6 [14172.5, 14354.6] | 15256.7 [15211.6, 15722.0] |

Every large foreign-resolution total is also slower above eight lanes in
each paired round, with all extra-lane ranges wholly above the eight-lane
range. Header processing still improves from twelve to fifteen lanes in
all three kinds and both paths, with separated ranges. The vector phase
medians below show that the total hides different scaling behavior:

| Foreign vector phase | 8 lanes | 12 | 15 |
| --- | ---: | ---: | ---: |
| dynamic headers | 92.5 | 79.2 | 65.9 |
| static headers | 134.5 | 121.7 | 100.5 |
| members headers | 125.9 | 106.4 | 89.3 |
| dynamic exchange | 1219.0 | 1865.2 | 2301.9 |
| static exchange | 1392.9 | 1984.3 | 2444.9 |
| members exchange | 1232.3 | 2524.2 | 1959.6 |
| dynamic resolve | 1405.9 | 911.3 | 948.1 |
| static resolve | 1446.2 | 970.0 | 936.8 |
| members resolve | 1920.0 | 1724.3 | 1610.7 |

Dynamic resolution is fastest at twelve lanes, with its fifteen-lane range
wholly slower, on both paths. Member resolution still improves at fifteen,
with a range wholly faster than twelve on both paths. Static resolution's
lowest median is at fifteen; the vector twelve/fifteen ranges overlap,
while the exact ranges are separated. Exchange remains more expensive than
eight lanes at every extra-lane count, even where the member case improves
from twelve to fifteen. These phase trends differ from the total's trend.

That result does not establish saturation of every phase. Shared-library
writer symbols fall from 75.2 [74.0, 77.4] us at eight lanes to
67.4 [66.8, 69.2] at twelve and 63.6 [61.9, 64.5] at fifteen, with separated
ranges. Their best observed value remains at the maximum available lane
count. No other writer phase has an extra-lane improvement with a wholly
separated faster range; the small apparent open/finish gains overlap their
eight-lane controls. No automatic dispatch threshold or production writer
change follows from these measurements.

All twelve direct invocations and three foreign sweeps completed successfully.
The final foreign sweep queued around 14:11 and completed by 14:57 UTC,
waiting mainly for CI.
At 14:57 UTC, the compiler hash, online CPUs, governors and filesystem
mounts still matched the initial metadata. The direct benchmark's programs ran
against their model and repeated build IDs matched outside timing. The
original 1/2/3/8-lane byte-identity tests remain the exact-byte evidence;
these timing runs do not add a cross-lane byte comparison at 10/12/15 lanes.

## Complete foreign links, 2026-10-24

The sweeps above time `foreign_run` alone, on a baseline from before the output
bridge. With `link-foreign` (#86) and the conversion on lanes
(`foreign_link_lanes.c`: the global names' symbols, the objects' maps, the
relocations and `link_check_lanes`), the whole foreign link runs on lanes, and
`--time` reports every phase: the resolution's on its slowest lane, the
conversion's and the writer's on lane 0, whose syncs end each pass after every
lane's part.

`link-foreign --time` linked synth's shape, `benchmark-link`'s 512 objects
(96 MB) of 102,400 workers, into a 49 MB dynamic PIE with #86's command line,
11 times at each lane count in each of three rounds: in the orders 1 to 15, 15
to 1, and 4, 1, 8, 2, 12, 3, 7, 15, 5, 10, 6, each round under its own lock
acquisition. N lanes ran on the first N CPUs of 2, 0, 1, 3, 4, 5, 6, 7, one per
core, then their SMT siblings 8, 9, 11, 12, 13, 14 and 15. The tree was #87's
`37dbecb`, before the cap below, which changes nothing here: every run had a CPU
per lane. Medians of the 33 links, in ms, the fastest in bold
([the record](https://github.com/buster14a/buster-performance/tree/d46536bfd7954ba539a29d23bf3db4f99cdf1445/docs/measurements/foreign-lanes-2026-10-24)):

| Phase | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 10 | 12 | 15 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| The whole link | 54.18 | 37.26 | 31.36 | 29.00 | 27.94 | 27.73 | **27.55** | 28.12 | 31.19 | 32.57 | 34.72 |
| Resolution | 21.25 | 13.32 | 11.12 | 10.13 | 9.65 | 9.74 | **9.64** | 9.92 | 11.42 | 12.09 | 13.38 |
| resolution: open | 1.38 | 0.996 | 0.852 | 0.768 | **0.747** | 0.748 | 0.760 | 0.771 | 0.856 | 0.890 | 0.955 |
| resolution: indices | **0.114** | 0.155 | 0.198 | 0.246 | 0.304 | 0.312 | 0.318 | 0.354 | 0.330 | 0.341 | 0.356 |
| resolution: headers | 0.494 | 0.248 | 0.152 | 0.125 | 0.114 | 0.101 | 0.089 | 0.084 | 0.083 | 0.071 | **0.064** |
| resolution: symbols | 6.65 | 4.32 | 3.38 | 3.04 | 2.97 | **2.85** | 2.91 | 2.87 | 3.24 | 3.56 | 4.15 |
| resolution: exchange | 2.35 | 1.85 | 1.79 | 1.79 | **1.76** | 1.83 | 1.94 | 2.11 | 2.54 | 2.87 | 3.36 |
| resolution: resolve | 7.14 | 3.79 | 2.89 | 2.43 | 2.02 | 1.99 | 1.68 | 1.74 | 1.99 | 1.91 | **1.28** |
| resolution: groups | 0.622 | 0.544 | 0.551 | 0.539 | 0.402 | 0.466 | 0.261 | 0.257 | 0.273 | 0.322 | **0.159** |
| resolution: winners | 2.47 | 1.41 | 1.15 | **1.07** | 1.15 | 1.18 | 1.41 | 1.53 | 1.79 | 1.73 | 2.58 |
| Conversion | 20.32 | 14.78 | 11.59 | 10.19 | 9.46 | 9.05 | 8.85 | **8.81** | 9.81 | 9.95 | 10.20 |
| conversion: sections | 1.29 | 1.29 | **1.26** | 1.27 | 1.32 | 1.28 | 1.33 | 1.36 | 1.53 | 1.61 | 1.55 |
| conversion: names | 7.10 | 3.61 | 2.60 | 2.14 | 1.91 | 1.74 | 1.70 | **1.65** | 1.92 | 1.99 | 2.15 |
| conversion: maps | 0.586 | 0.381 | 0.299 | 0.256 | 0.237 | 0.220 | 0.212 | **0.208** | 0.258 | 0.256 | 0.286 |
| conversion: relocations | 5.97 | 4.44 | 3.69 | 3.43 | **3.26** | 3.31 | 3.29 | 3.43 | 3.65 | 3.84 | 4.15 |
| conversion: copies | **0.348** | 0.348 | 0.348 | 0.348 | 0.348 | 0.348 | 0.352 | 0.352 | 0.360 | 0.361 | 0.363 |
| conversion: check | 4.89 | 4.58 | 3.24 | 2.59 | 2.20 | 1.95 | 1.78 | 1.63 | 1.90 | 1.70 | **1.47** |
| conversion: exports | **0.119** | 0.125 | 0.126 | 0.125 | 0.128 | 0.130 | 0.128 | 0.130 | 0.149 | 0.158 | 0.175 |
| Write | 9.66 | 6.23 | 5.70 | **5.62** | 5.66 | 5.77 | 5.90 | 6.00 | 6.70 | 7.26 | 7.84 |
| write: lanes and symbols | **0.045** | 0.050 | 0.051 | 0.052 | 0.056 | 0.067 | 0.073 | 0.082 | 0.113 | 0.130 | 0.151 |
| write: count | 1.45 | 0.821 | 0.603 | 0.511 | 0.459 | 0.425 | 0.412 | **0.405** | 0.509 | 0.504 | 0.512 |
| write: dynamic | 0.882 | 0.504 | 0.411 | 0.362 | 0.338 | 0.323 | 0.317 | **0.311** | 0.377 | 0.388 | 0.358 |
| write: chunks | 7.03 | 4.64 | **4.42** | 4.47 | 4.56 | 4.68 | 4.74 | 4.88 | 5.30 | 5.72 | 6.30 |
| Release | 2.83 | **2.77** | 2.79 | 2.89 | 2.92 | 2.89 | 2.88 | 3.00 | 3.00 | 2.96 | 3.00 |

Each phase's limit, where more lanes stop helping: its fastest lane count, and
the fewest lanes within 3% of that time.

| Phase | 1 lane | Fastest | Within 3% from |
| --- | ---: | ---: | ---: |
| The whole link | 54.18 ms | 27.55 ms on 7 | 5 |
| Resolution | 21.25 ms | 9.64 ms on 7 | 5 |
| resolution: open | 1.38 ms | 0.747 ms on 5 | 4 |
| resolution: indices | 0.114 ms | 0.114 ms on 1 | 1 |
| resolution: headers | 0.494 ms | 0.064 ms on 15 | 15 |
| resolution: symbols | 6.65 ms | 2.85 ms on 6 | 6 |
| resolution: exchange | 2.35 ms | 1.76 ms on 5 | 3 |
| resolution: resolve | 7.14 ms | 1.28 ms on 15 | 15 |
| resolution: groups | 0.622 ms | 0.159 ms on 15 | 15 |
| resolution: winners | 2.47 ms | 1.07 ms on 4 | 4 |
| Conversion | 20.32 ms | 8.81 ms on 8 | 6 |
| conversion: sections | 1.29 ms | not on lanes | 1 |
| conversion: names | 7.10 ms | 1.65 ms on 8 | 8 |
| conversion: maps | 0.586 ms | 0.208 ms on 8 | 7 |
| conversion: relocations | 5.97 ms | 3.26 ms on 5 | 5 |
| conversion: copies | 0.348 ms | not on lanes | 1 |
| conversion: check | 4.89 ms | 1.47 ms on 15 | 15 |
| conversion: exports | 0.119 ms | not on lanes | 1 |
| Write | 9.66 ms | 5.62 ms on 4 | 3 |
| write: lanes and symbols | 0.045 ms | 0.045 ms on 1 | 1 |
| write: count | 1.45 ms | 0.405 ms on 8 | 7 |
| write: dynamic | 0.882 ms | 0.311 ms on 8 | 7 |
| write: chunks | 7.03 ms | 4.42 ms on 3 | 3 |
| Release | 2.83 ms | not on lanes | 1 |

- **The whole link** is fastest on 7 lanes, 27.5 ms against 54.2 on one (2.0
  times), and within 3% of that from 5; the SMT siblings make it slower (34.7
  ms on 15). What doesn't run on lanes takes 4.7 ms at every count: the
  conversion's sections (1.3), copies (0.35) and exports (0.12), the writer's
  layout (0.08) and the release (2.8).
- **The conversion** takes 8.8 ms on 8 lanes against 20.3 on one: its names'
  symbols 1.65 against 7.10 (4.3 times), its relocations 3.26 on 5 against 5.97.
- **The writer's chunks** stop at 3 lanes (4.42 ms against 7.03): the page
  cache's copy bounds them, as the floors below found for `pwrite`.
- **The resolution's indices** are fastest on one lane (0.11 ms, 0.35 on 8):
  lane 0 lists the inputs and sets up the first round alone, the link's few
  archives leave the lanes little to read between, and its two syncs cost more
  with more lanes.
- **Four phases are still fastest on 15 lanes,** every CPU this machine has:
  the resolution's headers (0.064 ms), resolve (1.28) and groups (0.16), and the
  conversion's check (1.47). Their limit is past this machine's CPUs, and on it
  15: past its CPUs, lanes share them, and each sync waits for a lane the
  scheduler has set aside. Measured past them, on the tree before the cap: 16
  lanes on the 15 CPUs took 80.3 ms and 20 took 90.0, against 34.5 on 15;
  each sync waited about a slice, 1 ms (the writer's layout, symbols and open,
  0.03 to 0.10 ms on 15 lanes, took 1.0 each on 16), the conversion's check
  took 5.95 ms and the resolution's groups 2.16. The resolution's headers and
  resolve, whose timers end before their syncs, still read 0.064 and 1.22 ms
  on 16, their waits landing in the phases after them. `link-foreign` now runs
  at most a lane per CPU it may run on (and since #905, without `--lanes`, at
  most one per core), and `--time` names the lanes it ran on.
- **Small links want one lane:** the appendix's hello world took 0.60, 0.89,
  1.18, 1.24 and 1.76 ms on 1, 2, 3, 4 and 8 lanes, and tcc 1.09, 1.31, 1.47,
  1.72 and 2.18 (`timeit`, output on tmpfs); each lane's thread costs more than
  its share of the work.

The lane path makes the exact path's link. `test-foreign` compares the output of
1, 2, 3 and 8 lanes byte for byte, and `test-foreign-corpus` links each corpus
program on eight lanes as on one: on the 9700X, with SQLite's shell, Lua and
this compiler, 174 checks passed, and the job's 68 test commands passed.

## Filesystem floors

The updated benchmark separates plain mapping, mapping with `fallocate`,
mapping with `MADV_HUGEPAGE`, and mapping with both. It adds three lanes
and places `--cpu` first in the printed CPU order. After the final timed
write for every method, size and lane count, it checks file size and every
byte against the source, outside timing. Failed huge-page advice is reported
as a failed call rather than silently measured as success.

This is a final-output check, not proof that each timed write occurred.
In-place runs begin with the same source bytes already in the file, so a
skipped in-place write could leave matching bytes. The check deliberately
does not poison those pages between samples and change the measured write
state.

`--size-kb` and `--method` require `--only files` or `--only threads`.
The file sizes are 16, 256, 4096, 65536 and 524288 KiB. Threads support only
65536 and 524288 KiB and omit truncate/rename. Method names are
`new`, `truncate`, `in-place`, `rename`, `mmap`, `fallocate`,
`huge`, `fallocate-huge`, and `mmap-in-place`.

A bounded example, independently locked for each filesystem and repetition:

```sh
EXCLUSIVE_OWNER=codex-linker-scaling87 \
EXCLUSIVE_TITLE="Linker #87 tmpfs 64 MiB floors" \
EXCLUSIVE_LINK=https://github.com/buster14a/buster/issues/87 \
~/buster-fast-bench/exclusive.sh ./build/compiler benchmark-floors \
  --only threads --size-kb 65536 --cpu 2 \
  --directory /tmp/linker-scaling87-output
```

The thread matrix uses five inner samples and three outer repetitions on
each filesystem. `syncfs` follows each sample outside timing. Each timed
sample includes open, sizing, writes, unmap if needed, and close; thread
creation, prior-output unlink, and the opening broadcast are outside timing.
The single-thread files timer includes its method's unlink. The first
process also measured 512 MiB, but
that size has only one outer repetition and is exploratory. Logs are
`floors-FS-threads-r1.log` and `floors-FS64-rR.log`.

64 MiB medians of the three process medians, milliseconds:

| Method | tmpfs 1 | 2 | 3 | 4 | 8 | btrfs 1 | 2 | 3 | 4 | 8 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| New pwrite | 8.59 | 9.19 | 9.49 | 10.24 | 10.67 | 13.48 | 14.11 | 14.31 | 14.74 | 14.90 |
| In-place pwrite | 4.85 | 5.17 | 5.22 | 5.45 | 5.41 | 10.72 | 10.92 | 10.98 | 10.88 | 11.11 |
| New mmap | 12.87 | 8.59 | 7.07 | 6.46 | 5.44 | 20.26 | 14.93 | 13.09 | 13.60 | 16.40 |
| mmap + fallocate | 16.11 | 12.11 | 11.32 | 10.93 | 10.37 | 19.81 | 14.59 | 12.04 | 12.31 | 14.70 |
| mmap + huge advice | 12.82 | 8.46 | 6.85 | 6.14 | 5.45 | 20.34 | 14.41 | 12.58 | 13.56 | 16.30 |
| mmap + both | 16.40 | 12.04 | 11.25 | 10.92 | 10.39 | 19.99 | 14.02 | 11.67 | 12.10 | 14.61 |
| In-place mmap | 8.06 | 5.23 | 4.88 | 4.57 | 4.93 | 14.27 | 11.10 | 9.49 | 11.45 | 13.50 |

For 4 MiB, three `--only files --size-kb 4096 --cpu 2` processes on each
filesystem use fifteen inner samples. Medians below are microseconds; logs
are `floors-FS-files4-rR.log`.

| Method | tmpfs | btrfs |
| --- | ---: | ---: |
| New pwrite | 340.7 | 572.3 |
| Truncate | 337.5 | 3286.6 |
| In-place pwrite | 105.2 | 442.7 |
| Rename | 339.3 | 1074.8 |
| New mmap | 603.8 | 1036.4 |
| mmap + fallocate | 617.1 | 1007.6 |
| mmap + huge advice | 604.8 | 1033.6 |
| mmap + both | 612.9 | 1006.8 |
| In-place mmap | 320.6 | 724.5 |

At 4 MiB on one lane, pwrite beats mapping on both filesystems. At 64 MiB,
mapping new tmpfs pages scales, but reservation roughly doubles its eight-lane
cost. Huge-page advice adds no robust eight-lane tmpfs benefit. btrfs mapping
is best around three lanes and worsens at eight. These byte-copy floors do
not by themselves justify changing the linker.

## Integrated mapping experiment

The uncommitted prototype maps regular outputs of at least 4 MiB at two or
more lanes and copies each lane's existing iovec runs into that mapping.
The comparison uses the frozen baseline and identical generated inputs,
including hashing, relocation, file publication and finish costs. The plain
mapping compiler SHA-256 is
`50cad4b0e1f79712ef92a93f979c773cfb8a2bef23f9aa21acc0c8a4edbd3d23`.
Its patch is retained as `mmap-plain-experiment.patch`; it is excluded from
this change.

The direct command above ran at three and eight lanes on both filesystems,
with eleven inner samples and three outer repetitions. The first and third
outer repetitions run baseline then mapping; the second reverses that order.
The second also reverses filesystem and lane order. All default inputs and
four output kinds ran. Logs are `map-ab-FS-lN-VARIANT-rR.log`;
`mmap-summary.json` retains all 288 process results, including the smaller
inputs, and the 36 results of the reservation control below.

Large-input medians of three process medians, microseconds. Positive change
means slower; all individual process medians and their ranges are retained
in the JSON.

| Filesystem | Lanes | Kind | pwrite | Plain mmap | Change |
| --- | ---: | --- | ---: | ---: | ---: |
| tmpfs | 3 | dynamic PIE | 4408.9 | 4864.7 | 10.3% |
| tmpfs | 3 | static executable | 5231.7 | 5715.4 | 9.2% |
| tmpfs | 3 | static PIE | 5323.4 | 5830.1 | 9.5% |
| tmpfs | 3 | shared library | 12785.7 | 11090.1 | -13.3% |
| tmpfs | 8 | dynamic PIE | 4385.4 | 4106.0 | -6.4% |
| tmpfs | 8 | static executable | 5274.8 | 5193.0 | -1.6% |
| tmpfs | 8 | static PIE | 5323.9 | 5348.7 | 0.5% |
| tmpfs | 8 | shared library | 12791.6 | 9922.6 | -22.4% |
| btrfs | 3 | dynamic PIE | 7976.6 | 10083.8 | 26.4% |
| btrfs | 3 | static executable | 8840.4 | 11034.0 | 24.8% |
| btrfs | 3 | static PIE | 8960.4 | 11064.3 | 23.5% |
| btrfs | 3 | shared library | 23281.3 | 25101.8 | 7.8% |
| btrfs | 8 | dynamic PIE | 7939.1 | 13623.2 | 71.6% |
| btrfs | 8 | static executable | 8872.0 | 14762.5 | 66.4% |
| btrfs | 8 | static PIE | 8890.2 | 14638.5 | 64.7% |
| btrfs | 8 | shared library | 21745.1 | 26499.7 | 21.9% |

The global mapping candidate is rejected: every btrfs case regresses, well
beyond the observed spread. tmpfs is mixed: all three-lane in-place
executables regress, while shared-library publication improves by 13.3% at
three lanes and 22.4% at eight. Eight-lane static PIE does not improve.

A raw mapping can fault on exhausted storage. A second isolated candidate
uses `fallocate` before mapping to measure space reservation, keeping the
same mapping gate and copying code. Its compiler SHA-256 is
`d316e45f5e95a4c3bd682441723577a506d65df4745c1c67daa1d9ad152c91c4`;
the patch is `mmap-reserved-experiment.patch`. This control uses the same
direct command with `--kinds shared`, on tmpfs at three and eight lanes,
eleven inner samples and three outer repetitions with the middle order
reversed. These are the plain candidate's largest whole-pipeline gains.
Logs are `map-reserved-tmpfs-lN-VARIANT-rR.log`.

| Lanes | pwrite median (range), us | Reserved mmap median (range), us |
| --- | ---: | ---: |
| 3 | 12705.1 (12606.2–12859.7) | 14091.8 (14067.3–14138.8) |
| 8 | 12549.3 (12444.6–13052.1) | 13054.2 (12998.3–13303.1) |

Reservation erases that publication gain: every paired control is slower
with reserved mapping, by 10.9% at three lanes and 4.0% at eight on the
medians. The eight-lane ranges overlap; they establish no reserved-mapping
win. The existing writer is retained, and neither experiment is included
in the committed code. This does not rule out every narrower future policy
or filesystem; such a policy still needs its own repeated whole-pipeline
measurements and failure/cleanup tests.

## Correctness and remaining work

The frozen baseline passed `test-lanes --seed 87`, `test-executable`,
`test-object-writer`, `test-dynamic`, `test-static`, and `test-foreign`,
each separately locked. Existing deterministic checks passed at 1, 2, 3 and
8 lanes. Lane exchange records and winners had zero differences; the object
writer's 25-unit case produced byte-identical objects at every count.
Logs are `test-*-base.log`.

The plain mapping experiment passed `test-executable`, `test-dynamic`,
and `test-static`; its whole-link benchmark outputs ran and preserved their
build IDs. The executable suite's 12,377,016-byte many-chunks case crosses
the mapping size gate at 2, 3 and 8 lanes and compares every byte with its
one-lane pwrite result; those checks passed. All
filesystem floor final-byte checks passed. Invalid filter combinations and
unknown methods failed with status 1; a valid small huge-advice file case
completed with status 0.

The reservation candidate also passed `test-executable`, including that
many-chunks byte comparison. Its log is `test-executable-mmap-reserved.log`.

The committed result is better measurement and reproducible evidence. With
the complete foreign links' sweep, every phase has a measured limit on this
machine, those still fastest at 15 lanes bounded by its CPUs. It does not
establish scaling past this machine's eight physical cores, or a production mmap
policy: the writer keeps pwrite.
