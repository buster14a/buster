# Linker lanes and filesystem writes

This records the frozen 9700X measurements for [#87](https://github.com/buster14a/buster/issues/87),
taken on 2026-10-23. It adds independently controlled filesystem factors and
bounded runs to `benchmark-floors`; it does not change the production writer.

The large direct pipeline reaches a broad plateau around 3–4 lanes on this
host. Foreign exchange stops helping earlier than resolution. Several other
phases still improve at the eight physical cores available here, so their
saturation beyond eight lanes is not established. Issue #87 remains open for
those limits and a justified production write policy.

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
  CPU 2 used the performance governor; CPUs 0, 1, 3–7
  used powersave. CPU 10, CPU 2's SMT sibling, was offline. These settings
  were observed, not changed for the experiment.
- Each lane count uses the prefix of CPUs `2,0,1,3,4,5,6,7`: one physical
  core per lane, CPU 2 for one-lane timing. Other core governors and natural
  barriers are part of this measured configuration; the numbers do not
  describe eight cores held artificially at a fixed clock.
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
`floors64-summary.json`, and `floors4-summary.json` retain the parsed
process medians. These are retained experiment artifacts, not a hosted CI
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

### Whole-pipeline results

Each cell is the median of three process medians, in microseconds, for the
102,400-worker input. The direct total is the sum of independently measured
part medians, as `benchmark-link` defines it, rather than a single timed
wall-clock interval.

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
exchange and consumer separately while retaining the whole-pipeline result;
it is not evidence that exchange can be removed.

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

The committed result is better measurement and reproducible evidence. It
does not establish saturation beyond eight physical cores, universal phase
thresholds, a production mmap policy, or post-change hosted CI success.
