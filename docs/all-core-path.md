# The all-core path: a compile whose working memory is a window of the unit (#2536): design

Status: design. Step 0, the gauge, is built: `tools/allcore/allcore.c`
([its guide](commands/all-core.md)), measured against its gate in
[its record](https://github.com/buster14a/buster-performance/tree/359dcadc4a98855be571e0e7b64c50c44d82c9c9/docs/measurements/all-core-gauge-2536-2026-11-12).
Nothing else below is implemented, and its gains are a model's that its first
steps exist to confirm or refute. It is
[#672](https://github.com/buster14a/buster/issues/672)'s all-core target's
design: what eight workers cost a compile on the 9700X, what the designs
already in hand remove of that, and the change that removes the rest. The issue
is [#2536](https://github.com/buster14a/buster/issues/2536). The names of
existing code are main's at `4c8c3c9c`, and every figure not attributed
otherwise is that revision's on the 9700X, under the lock, from
[the measurement record](https://github.com/buster14a/buster-performance/tree/222f622d5f7d46abbc333d80ec8e6ce91930fc4b/docs/measurements/all-core-rerun-2536-2026-11-12),
which measured [the first record](https://github.com/buster14a/buster-performance/tree/eaa710db255e42520666aaea225b142aa74a2bde/docs/measurements/all-core-design-672-2026-11-12)
again once the stray process of [#2555](https://github.com/buster14a/buster/issues/2555)
was gone.

**In one paragraph.** Eight compiles of `sqlite3.c` at once cost each of them
1.97 times the cycles of one alone (1,350 M against 687 M), for the same
instructions: 289 MB/s where eight times one compile would be 585. All of the
difference is memory. Twice as many loads miss every cache, since the
third-level cache is shared by eight, and each waits a quarter longer: loads
that aren't complete are 419 M of the 466 M more user cycles. And the kernel's
work for the 190 MB of pages a compile touches once triples. The stages whose
loads reach all over the unit inflate most: the parse 3.4 times, `check-input`
1.9, `check-program` 1.8, the lowering 1.6. Tier 0 and the encoder, which stay
inside one function, cost 1.2 times. The design makes the rest of the compile
run as they do: the unit is read one **window** of external declarations at a
time, each window taken from its tokens to its code before the next is
touched, in memory that the next window reuses and that stays in the
second-level cache. Today's order is the same code with one window as long as
the unit: the twin, and the oracle. It composes with the two one-core
designs, [the node machine](node-machine.md) for the bodies and
[attested declarations](attested-declarations.md) for the file scope: they
remove work, and this removes the waiting for what is left. Expected for
eight workers on `sqlite3.c`: 2.0 times main's throughput from the windows
alone (567 MB/s), 2.1 from the node machine alone, and 3.6 to 4.0 from both,
1.0 to 1.1 GB/s.

## The problem

### What eight workers cost a compile

`-c --lanes 1` of the frozen `sqlite3.c` (9,507,037 bytes, 716,136
preprocessed tokens, the 9700X job's `compile_time` options), as independent
processes pinned one a physical core, six compiles a worker one after another,
each run counted by counters of its own that its exit helper inherits. Medians
over six rounds of every worker's runs but its first and last, per compile:

| workers | MB/s | wall ms | user cycles | kernel cycles | L2 fills from DRAM | peak RSS | faults | GHz |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 1, CPU 2 | 73.2 | 125.0 | 601.5 M | 85.0 M | 13.1 M | 241.0 MB | 7,404 | 5.55 |
| 1, CPU 5 | 75.3 | 125.5 | 602.4 M | 83.1 M | 13.1 M | 241.0 MB | 7,404 | 5.51 |
| 2 | 125.6 | 143.1 | 679.6 M | 100.0 M | 15.0 M | 236.7 MB | 7,460 | 5.52 |
| 4 | 198.8 | 188.0 | 822.7 M | 194.9 M | 15.1 M | 195.2 MB | 62,964 | 5.50 |
| 8 | 288.8 | 259.0 | 1,074.4 M | 275.6 M | 15.3 M | 195.2 MB | 62,982 | 5.28 |
| 15, every online CPU | 321.1 | 446.7 | 1,870.7 M | 420.8 M | 15.6 M | 195.2 MB | 62,970 | 5.21 |

Every compile retires the same 1,348.9 M user instructions. Eight workers give
3.95 times one worker's throughput; each compile costs 1.79 times the user
cycles and 3.24 times the kernel's, 1.97 times in all. The second thread of
each core adds 11%. CPU 2 alone runs the performance governor, and a compile
alone on a powersave core (CPU 5) is 0.4% slower; nothing here changed a host
setting. From four workers on, a compile finds the host busy when it starts
and takes base pages (#1561): that is why the peak and the faults change
there. Two workers are at the test's edge (runnable tasks a quarter of the 15
CPUs): 46 of their 72 compiles took huge pages, and the row is theirs; the 26
that took base pages cost 163.9 ms, 722.7 M user and 166.1 M kernel cycles.
The peaks include about 4.5 MB of the executable's own file, which the
measuring copy of it maps and the first record's build didn't (the record
checks it): a compile's own peak is 236.7 MB alone and 190.7 MB in base pages,
as before.

The first record of this design was measured beside a stray process (a test
program left looping by another session outside the lock, #2555). It shared
core 0 with a worker, which the first record left out, and the slowed worker
left the others a lighter host: eight workers there cost 1,297 M cycles a
compile and ran 295 MB/s. It was also one more runnable task, which put the
two-worker cohorts over the busy test. The figures here are from the same
executable, scripts and inputs once the stray was gone.

For 1 GB/s on eight cores a compile has 76 ms of a core, 401 M cycles at the
frequency eight workers run at. Today's 1,350 M is 3.4 times that.

### All of it is memory

The same cohorts under three other counter sets, per compile:

| | 1 worker | 8 workers |
| --- | ---: | ---: |
| user cycles | 601.6 M | 1,067.4 M |
| … with a load not complete at retirement | 248.9 M | 668.2 M |
| dispatch slots waiting for the backend | 39.7% | 60.4% |
| L1D demand fills: all; from L3; from DRAM | 11.38 M; 1.22 M; 1.43 M | 10.93 M; 0.81 M; 3.01 M |
| L2 fills from L3, user | 8.68 M | 2.44 M |
| L2 fills from DRAM: user; kernel | 7.99 M; 5.10 M | 10.15 M; 5.09 M |
| L3 lookups that miss; a miss's sampled latency | 59%; 116 ns | 85%; 150 ns |
| mispredicted branches | 3.62 M | 3.70 M |

- **The waiting is the inflation.** 419 M of the 466 M more user cycles have a
  load that isn't complete. The branches, the instructions and the first-level
  cache's misses are the same.
- **Twice the misses, each a quarter longer.** Alone, a compile has the 32 MB
  third-level cache to itself; beside seven others it has an eighth. The
  demand fills that come from DRAM double (1.43 M to 3.01 M) and the L3's own
  fall by a third. An L3 miss's sampled latency (`l3_xi_sampled_latency`, on
  every CPU) is 116 ns with one worker, 127 with two (in huge pages), 120 with
  four, 150 with eight and 166 with fifteen, against 79 on the idle host.
- **The kernel's part is the pages.** A compile touches 190 MB of fresh pages
  once: 63,000 faults, each with its clear, its charge and its place in the
  kernel's lists, and at its end as many pages given back. That is 157 M
  kernel cycles alone with base pages and 276 M beside seven others: 4,400
  cycles a page, 1.4 M for each megabyte of the peak. The exit alone, where
  eight processes give their pages back at once, is about 33 M of them, and
  `native_queued_spin_lock_slowpath` leads its teardown.
- **It isn't the clearing that grows.** By the kernel's functions, in sampled
  cycles a compile at eight workers: `_raw_spin_lock` 56 M,
  `do_anonymous_page` 53 M, `clear_page_erms` 52 M, `lock_vma_under_rcu`
  33 M, `asm_exc_page_fault` 21 M, `native_queued_spin_lock_slowpath` 15 M.
  Alone, with huge pages, `clear_page_erms` is 57 M and nothing else is above
  4 M. The clear of a page costs what it did; the fault's own path and its
  locks, taken 63,000 times beside seven other processes doing the same, are
  the rest.

### By stage

Each compile's own `--time-stages` report gives the stages' times; the
counters by stage are
[the one-core record](https://github.com/buster14a/buster-performance/tree/5318c1ea0e3a39f22f01b2bcb837db112c2a8fa7/docs/measurements/one-core-design-672-2026-11-12)'s
method run on every worker's CPU at once (`perf record -a` of a leader-sampled group with
sample times, each sample put in the run and the stage its time falls in).
Per compile, one worker against eight:

| stage | ms | user cycles, M | times | with a load not complete, M | kernel cycles, M | demand fills from DRAM, thousands | pages faulted, 8 | DRAM bytes moved, MB |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| preprocess | 9.7 → 15.7 | 43.2 → 60.3 | 1.40 | 8.9 → 23.7 | 10.0 → 18.7 | 13 → 53 | 5,639 | 96 → 80 |
| parse | 7.3 → 24.5 | 23.0 → 78.8 | 3.43 | 4.0 → 53.7 | 17.3 → 46.9 | 11 → 230 | 10,430 | 183 → 151 |
| check-input | 46.1 → 106.3 | 217.9 → 421.3 | 1.93 | 94.1 → 259.5 | 33.1 → 127.7 | 770 → 1,257 | 33,430 | 609 → 576 |
| check-expressions | 11.4 → 20.9 | 58.1 → 86.6 | 1.49 | 19.4 → 44.2 | 5.0 → 17.8 | 37 → 129 | 6,649 | 102 → 113 |
| check-declarations, static-data | 2.5 → 3.9 | 12.6 → 18.8 | 1.49 | 7.2 → 13.4 | 1.0 → 3.6 | 46 → 58 | 549 | 34 → 25 |
| check-program | 23.9 → 46.5 | 128.6 → 232.2 | 1.81 | 68.9 → 163.4 | 1.9 → 9.1 | 381 → 824 | 2,546 | 214 → 252 |
| select | 1.1 → 2.8 | 5.0 → 12.1 | 2.43 | 2.5 → 8.6 | 1.0 → 2.4 | 21 → 66 | 782 | 12 → 15 |
| lower | 11.5 → 19.8 | 61.7 → 98.3 | 1.59 | 26.8 → 61.4 | 1.6 → 4.2 | 97 → 255 | 2,147 | 71 → 88 |
| the emission, Tier 0 to the code's atoms | 8.6 → 12.2 | 45.6 → 59.3 | 1.30 | 12.3 → 24.3 | 1.3 → 1.5 | 37 → 95 | 298 | 32 → 51 |
| the object | 1.5 → 2.7 | 4.7 → 4.9 | 1.04 | 1.3 → 2.3 | 5.3 → 7.8 | 8 → 21 | 37 | 13 → 9 |
| the process's start and end | 0.3 → 0.3 | 1.1 → 1.8 | | 0.3 → 0.7 | 7.6 → 35.9 | 2 → 2 | 382 | 20 → 27 |
| whole compile | 123.7 → 255.6 | 601.5 → 1,074.4 | 1.79 | 245.9 → 655.1 | 85.0 → 275.6 | 1,421 → 2,991 | 62,889 | 1,386 → 1,387 |

The emission's stages interleave function by function, so they are one row.
The user cycles are the sampled cohorts' scaled to the unsampled medians, and
the kernel's are less the sampling's own. The pages are base pages at eight
workers; alone, the kernel clears huge pages instead (7,404 faults).

The same compile's function bodies, by family of function (the unit whole
less the unit without its bodies, the one-core record's split and families),
in user cycles a body token:

| family | alone | at eight | times |
| --- | ---: | ---: | ---: |
| the parser and the tree's columns | 43.7 | 144.4 | 3.30 |
| the reader's other functions | 157.1 | 285.8 | 1.82 |
| the graph's construction | 98.0 | 175.9 | 1.80 |
| the program and its controls | 126.0 | 225.5 | 1.79 |
| libc's `memset`, `memmove`, `memcmp` | 25.5 | 44.2 | 1.74 |
| warnings | 123.3 | 205.7 | 1.67 |
| initializers and static data | 24.3 | 39.9 | 1.64 |
| the lowering | 93.0 | 142.5 | 1.53 |
| the C rules for a node | 31.8 | 41.2 | 1.30 |
| the graph run's loop | 52.0 | 66.6 | 1.28 |
| the emission: Tier 0, the encoder, branch sizing, unwinding | 75.8 | 90.9 | 1.20 |
| all of the bodies | 879 | 1,570 | 1.79 |

- **What inflates is a load the prefetchers can't guess, into a record
  longer than the cache.** The parser's passes scatter into arrays as long as
  the unit (`parse_tree_columns`, `parse_top_parents8`): 3.3 times. The
  reader's and the program's walks follow the tree and the graph: 1.8. The
  warnings and the lowering read the graph a node at a time: 1.5 to 1.7.
- **What doesn't is a pass that streams, or one that stays in a function.**
  The graph's run reads its nodes in order and writes the stream behind it,
  and costs 1.28 times. Tier 0 and the encoder take one function's typed
  entries to its code and touch nothing else: 1.20, and what they do wait
  for is the stream that the lowering wrote for the whole unit before them.
- **By stage, the kernel's cycles follow the pages** the stage is first to
  touch: `check-input` 128 M for 33,430 pages, the parse 47 M for 10,430.

### The page policy apart from the load

One worker and eight differ in two things, the load and the pages, so the
other two corners were run too (`BUSTER_HUGE_PAGES=0` alone, `=1` at eight):

| | wall ms | user cycles | kernel cycles | load not complete | DRAM bytes moved | MB/s |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 1 worker, huge pages (main) | 125.0 | 601.5 M | 85.0 M | 248.9 M | 1,380 MB | 73.2 |
| 1 worker, base pages | 149.2 | 658.7 M | 156.6 M | 301.5 M | 1,155 to 1,184 MB | 62.6 |
| 8 workers, base pages (main) | 259.0 | 1,074.4 M | 275.6 M | 668.2 M | 1,388 MB | 288.8 |
| 8 workers, huge pages | 289.4 | 1,239.5 M | 278.5 M | 814.2 M | 1,733 MB | 267.2 |

With the pages held the same, eight workers cost 1.63 times the user cycles
and 1.76 times the kernel's in base pages, and 2.06 and 3.28 times in huge
pages. #1561's policy is still right at this main: 8.1% more throughput at
eight workers, where base pages alone would cost one compile 19%. Neither
corner is the fix: the inflation is 1.6 times with the better pages.

### DRAM traffic

By the memory controllers' counters (`amd_umc` read and write CAS commands, 64
bytes each, on every CPU, less a 2 s idle control; #672's claim holds physical
traffic conditional on
[buster-performance#49](https://github.com/buster14a/buster-performance/issues/49),
and so does this):

| workers | read, MB a compile | written | bytes a source byte | GB/s over the cohort | the data bus in use |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | 875 | 507 | 145 | 11.0 | 13% |
| 2 | 948 | 480 | 150 | 18.5 | 22% |
| 4 | 972 | 397 | 144 | 28.7 | 34% |
| 8 | 983 | 406 | 146 | 42.0 | 50% |
| 15 | 1,008 | 414 | 150 | 46.1 | 55% |

A compile moves 1.39 GB through DRAM whether it runs alone or beside seven
others: 146 bytes for each source byte (#672's first record had 269, and
217 two days later). Eight at once would ask the bus for eight times one
compile's rate, 89 GB/s, where two channels of DDR5-5200 carry 83.2 at most;
they get 43 GB/s while all eight run (42.6; the median of the counters read
every 2 ms is 43.5, and the cohort's own mean 42.0; fifteen workers' is 46),
and slow down until their traffic fits. So #672's model still holds: 42.6 GB/s
over 146 bytes a byte is 292 MB/s, where 289 were measured. Streams over 1 GB
a worker on the same eight cores, the same day, move 55 GB/s reading, 52 with
ordinary stores and 57 copying (one core alone: 48, 46 and 51): the compiles
use three quarters of what a stream gets.

By stage, per compile at eight workers (each 2 ms interval's count divided
among the stages its runs were in; the stage table's last column):
`check-input` 576 MB, `check-program` 252, the parse 151, `check-expressions`
113, the lowering 88, the preprocessor 80, the whole emission 51.

### The bodies and the file scope at eight workers

The unit preprocessed, compiled whole and with each of its 2,674 function
bodies replaced by `;` (the one-core record's split), at one worker and at
eight:

| per compile | whole, 1 | file scope, 1 | bodies, 1 | whole, 8 | file scope, 8 | bodies, 8 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| wall ms | 116.9 | 18.9 | 98.0 | 245.8 | 36.7 | 209.1 |
| user cycles | 562.2 M | 74.3 M | 487.9 M | 1,021.9 M | 126.3 M | 895.6 M |
| kernel cycles | 81.3 M | 26.9 M | 54.4 M | 258.4 M | 55.5 M | 202.9 M |
| load not complete | 236.9 M | 19.2 M | 217.7 M | 656.9 M | 63.7 M | 593.2 M |
| peak RSS | 233.2 MB | 89.4 MB | | 185.2 MB | 48.4 MB | |
| DRAM bytes moved | 1,343 MB | 222 MB | 1,121 MB | 1,362 MB | 179 MB | 1,183 MB |

The bodies are 86% of a compile's cycles at eight workers, and 84% alone.
By stage, of the compile's 115.8 ms alone and 242.3 ms at eight workers: the
bodies' part of the stages between the parser and Tier 0 (`check-input` to
`lower`, the node machine's territory) is 81.5 and 167.4 ms; of the parse,
5.6 and 20.8; of Tier 0 and the emission, 8.3 and 12.2. The file scope alone, a sixth of the
unit's time, still inflates 1.70 times in user cycles: 150,169 tokens of
declarations are 44 MB of fresh pages, far past any cache.

### musl: units on lanes, units as processes

The frozen corpus's musl, the first source of each base name: 1,336 units,
5,039,561 tokens, 61% of them file scope. To objects five ways, per pass over
the units:

| way | pass ms | units/s | user cycles | kernel cycles | L2 fills from DRAM | faults |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| one command, one lane (CPU 2) | 1,054 | 1,268 | 4,774 M | 1,050 M | 7.6 M | 343,084 |
| one command, 8 lanes | 207 | 6,444 | 5,676 M | 2,265 M | 47.9 M | 327,488 |
| 8 commands, a share of the units and one lane each | 212 | 6,313 | 5,219 M | 1,417 M | 39.1 M | 386,350 |
| a process a unit, one at a time | 2,217 | 603 | 5,812 M | 4,072 M | 36.0 M | 805,820 |
| a process a unit, 8 at a time | 405 | 3,297 | 6,712 M | 5,342 M | 87.8 M | 1,743,541 |

- **Eight lanes are 5.1 times one lane**, for 1.36 times the cycles: the user
  cycles rise 1.19 times and the kernel's 2.16. A pass moves 1.3 GB through
  DRAM on one lane and 4.9 GB on eight.
- **The user side barely inflates** (the table below). A unit of musl is
  3,800 tokens: its records are in the
  caches already. A unit's own compile as a process each costs 1.15 times the
  user cycles at eight at a time; the cluster of 2,000 tokens of
  [#672's experiment](https://github.com/buster14a/buster/issues/672#issuecomment-6810078226)
  cost 1.08 to 1.15 times, and a unit of `sqlite3.c`'s size 1.84.
- **Lanes against processes.** The same units as eight commands take about
  the same time (2% longer: a pass is its slowest share's) for 16% fewer
  cycles: 0.92 times the lanes' user cycles and 0.63 times their kernel
  cycles. What the lanes pay for one address space is 850 M kernel cycles a
  pass: the TLB flushes sent to the other lanes when a unit's arenas are given
  back are 776 M sampled cycles a pass on eight lanes and 15 M as eight
  processes, and the pages come back to be cleared cold (192 M against 99 M)
  ([#1213](https://github.com/buster14a/buster/issues/1213)).
- **A process a unit** costs 1.70 times the cycles of one command alone and
  2.07 times at eight at a time, and takes 2.0 times as long as the command
  on lanes: 3.0 M cycles of start and end for a unit whose own compile is
  4.4 M ([#1444](https://github.com/buster14a/buster/issues/1444)).

A command of several units reports no stages, so its inflation is by family
of function: sampled user cycles a pass, one cohort each way (their totals
inflate 1.16 times where the unsampled medians above inflate 1.19).

| family, user cycles a pass | one lane | 8 lanes | times |
| --- | ---: | ---: | ---: |
| the reader's functions | 1,489 M | 1,622 M | 1.09 |
| the preprocessor and the lexer | 817 M | 1,000 M | 1.22 |
| the emission | 330 M | 343 M | 1.04 |
| the parser and the tree's columns | 288 M | 304 M | 1.06 |
| libc's `memset`, `memmove`, `memcmp` | 248 M | 338 M | 1.37 |
| the graph's construction | 225 M | 259 M | 1.15 |
| warnings | 180 M | 195 M | 1.08 |
| the program and its controls | 172 M | 191 M | 1.11 |
| the lowering | 105 M | 129 M | 1.22 |
| the type table | 104 M | 103 M | 0.99 |
| initializers and static data | 97 M | 103 M | 1.07 |
| the graph run and its loop | 83 M | 90 M | 1.08 |
| the C rules for a node | 65 M | 76 M | 1.16 |
| the rest of the compiler | 578 M | 616 M | 1.07 |
| samples at the kernel's addresses | 186 M | 389 M | 2.09 |
| all | 4,967 M | 5,758 M | 1.16 |

And the kernel, by family, in both modes' sampled cycles a pass:

| family | one lane | 8 lanes | 8 commands |
| --- | ---: | ---: | ---: |
| TLB flushes sent to other CPUs | 13 M | 776 M | 15 M |
| page faults | 458 M | 573 M | 646 M |
| pages given back | 286 M | 403 M | 431 M |
| clearing pages | 86 M | 192 M | 99 M |
| spin locks | 162 M | 220 M | 236 M |
| sleeping locks (the address space's) | 6 M | 39 M | 9 M |

### What the designs in hand remove at eight workers

Each as a share of what a compile costs at eight workers (1,350 M cycles for
`sqlite3.c`, 7,941 M a pass for musl's command on eight lanes): measured where
it could be, and by [the model](#the-model) where it is a design's estimate.

| design | `sqlite3.c`, eight processes | musl's command, eight lanes |
| --- | --- | --- |
| **The node machine**, [#2524](https://github.com/buster14a/buster/issues/2524): the bodies between the parser and Tier 0 | Its territory is 913 M of the 1,350 M, 68% (64% alone): 3.1 times if it were free. The cycles a body token that its steps 2, 3 and 4 stop spending are 613, 292 and 330 at eight workers, where they are 383, 171 and 183 alone. With its own coverage and cost: **1.37, 1.55 and 2.15 times** after those steps (395, 448 and 620 MB/s). | The families it removes inflate 1.1 to 1.25 times on lanes, as the whole pass's user side does: about 1.1 times, as alone. |
| **Attested declarations**, [#2531](https://github.com/buster14a/buster/issues/2531): a file-scope declaration read once a command | Nothing: a command of one unit has no memo. | The reader's functions are 1,622 M of a pass's 5,758 M sampled user cycles and inflate 1.09 times on lanes, where the pass's cycles inflate 1.36: the file scope's reading falls from 32% of the command to about a quarter, and the 1,099 M cycles a pass that its skip measures alone are about 1,250 M of the lanes' 7,941 M. **About 1.2 times** by that estimate, where the skip measures 1.25 alone; it wasn't run on lanes. |
| **The windows**, [#672's research](https://github.com/buster14a/buster/issues/672#issuecomment-6749355847): this design | It removes 642 M of the 1,350 M after its five steps, expected, and leaves 708 M: **1.96 times** (567 MB/s). | Little on the user side: the units are a window each already. The bodies' records leave the arenas a unit gives back. |
| **The lanes' kernel time**, [#1213](https://github.com/buster14a/buster/issues/1213) | Nothing: no lanes. | 848 M kernel cycles and 457 M user cycles a pass against the same units as eight processes: **1.20 times**. |
| **A process's start**, [#1444](https://github.com/buster14a/buster/issues/1444) | 0.3%. | Nothing in one command. Of a process a unit, eight at a time: 45% of 12,054 M cycles. |
| **The peak**, [#1701](https://github.com/buster14a/buster/issues/1701) | 1.4 M kernel cycles a megabyte: the 190.7 MB are 276 M, a fifth of the compile. | The kernel is 2,265 M of a pass's 7,941 M, 29%. |
| The asm matcher's index, [#2532](https://github.com/buster14a/buster/issues/2532) | Nothing: no asm statement. | 7.9% of the command alone (its issue); baked since, 1.07 times alone at `0af27a30`; not measured on lanes. |

So at eight workers the node machine ranks first for `sqlite3.c` (2.15 times
after its last step), the windows second (1.96), and no design in hand removes
what the other leaves: the machine takes the bodies' middle out of both the
waiting and the pages, and what stays (the parse, the file scope, the
preprocessor, the bodies it refuses, the emission) is 628 M cycles a compile
that still inflate as they do today. For musl's command the order is attested
declarations, the lanes' kernel time, then the node machine.

## The design

### A stage boundary over the unit is a pass through DRAM

Each stage writes a record of the unit that the next one reads: the
preprocessor's token columns, the parser's tree, the reader's token columns,
the graph, the stream's values, the program's rows, the typed stream. Each is
megabytes (the reader's 88 MB, the graph's 34 MB, the tree's 16 MB, the
tokens' 12.5 MB), so by the time the next stage reads a line of it, the line
has left the caches. Alone, the third-level cache gives part of it back.
Beside seven others, twice as much of it comes from DRAM, and more slowly.

This is the order that
[#672's research](https://github.com/buster14a/buster/issues/672#issuecomment-6749355847)
proposed as function-major windows, and
[its experiment](https://github.com/buster14a/buster/issues/672#issuecomment-6810078226)
didn't refute. What this document adds is the measurements that rank it on
real code, how it meets the two one-core designs, the mechanism that lets
today's stages run on a window, the unit's facts a window can't know, and
steps with gates.

The emission shows the other order. For one function at a time it takes the
typed entries to x86 records, sizes the branches and encodes, and each of
those steps reads what the one before wrote microseconds ago. It moves 51 MB
of the compile's 1,387, and inflates 1.2 to 1.3 times, most of that for the
one unit-wide record it does read.

So the design doesn't make a stage faster. It changes the order the stages
run in, from every stage over the whole unit to every stage over a small part
of it, and gives that part's records memory that the next part reuses.

### The order: a spine, a chain of windows, an epilogue

```text
spine     the file scope in source order: each external declaration read into
          the environment; a function definition's declarator too
window    consecutive external declarations of about W tokens; a function
          body longer than W alone
chain     for a window, each of its bodies from its tokens to its code:
            tokens -> tree -> [the node machine | the reader's walks, the
            graph, the graph's run, the warnings, the program, the lowering]
            -> Tier 0 -> branch sizes -> encoding
          then the window's memory is the next window's
epilogue  what needs the whole unit: which internal functions are emitted,
          the unit-wide warnings, tentative definitions, the diagnostics'
          order, the object
```

- **The spine is the ordered part.** C fixes the order of the file scope: a
  declaration changes what the next one means. The spine reads it in that
  order and appends to an **environment**: the names' bindings, the types,
  the objects and their linkage, each record with the position it took effect
  at. It is #2524's step 4's boundary and
  [attested declarations](attested-declarations.md)' too: a body binds a
  file-scope name through `c_environment_lookup`, at its own position.
- **A window's bodies are independent of each other.** A body reads the
  environment as it stood at its opening, and writes its code, its
  diagnostics and a short list of effects on the file scope (a block-scope
  `extern`, an implicit function's declaration, the declarations it used).
  On one lane the chain follows the spine window by window, so the
  environment a body sees is simply what has been read. On several lanes the
  spine runs ahead and lanes take windows behind it, each with the
  environment's length at its window's opening as its watermark.
- **W is the second-level cache.** The 9700X's is 1 MB a core. Today's stages
  hold about 270 bytes of records a token (190 MB for 716,136 tokens), so a
  window of 2,048 tokens is about half of it, and that is W until the records
  shrink. `sqlite3.c`'s bodies are 213 tokens on average; its largest, 28,599
  tokens, is a window of its own of 8 MB, past the core's share of the
  third-level cache. A body the node machine takes needs 28 bytes a node and
  its entries.
- **One window as long as the unit is today's compile.** `W = ∞` runs every
  stage over the whole unit, in the unit's memory, as main does. It is the
  twin of every finite W, and the oracle: the object and the diagnostics must
  be the same bytes for every W, on the vector path and with `--scalar`.

### A window's memory

- **One arena a lane, reserved once, never given back.** A window's records
  are pushed into it and popped at the window's end (`arena_pop_to`); the
  next window's pushes find the same pages, resident and in the cache. No
  page is faulted after the first windows, none is cleared by the kernel, and
  none is returned before the process ends. This is the one place where the
  arenas' rule changes: a popped arena's bytes are not zero (`arena_push`
  says so already), so a stage that reads a column before it writes it clears
  what it will read, and the clear is of a window's length, in the cache. The
  kernel's work for 190 MB of pages is 276 M cycles a compile at eight
  workers; a `memset` of every window's records, if every stage needed one,
  would be about 6 M. It is the retained workspace that
  [#1213's design](https://github.com/buster14a/buster/issues/1213#issuecomment-6721231167)
  allows: an owner of its own with a stated overwrite or reset contract, not
  dirty arenas in the general pool, which go on promising zeros. The
  experiment that issue rejects kept every mapping and cleared every pushed
  byte, megabytes of them cold; a window's are a cache's length, and step 1's
  gate is where that is measured.
- **The pages are base pages.** A window's arena is at most a few megabytes
  and is touched densely: the busy host's policy and the idle host's agree on
  it, and #1561's test of the load is left to what stays unit-sized.
- **The unit's indices don't change.** A token keeps its index in the unit, a
  node its number, a row its place: diagnostics, `-g`'s rows and the oracle's
  comparison name them. A window's column is a short array that stands for
  the range `[first, first + count)` of the unit's: its base is biased by
  `first` elements, so the stages' loads and stores (`column[token]`) are the
  same code for a window and for the unit, and for `W = ∞` the bias is zero.
  A stage that reaches outside its window's range through such a column has
  a bug that the unit-wide path would hide; the window's arena is bracketed
  by inaccessible pages in the tests (`--poison-arenas`' kin), so that it
  faults.
- **What a window keeps** is copied out before the pop: its code, its
  relocations, its unwind rows and `-g`'s rows to the unit's output arenas
  (which the object writer reads once, in order); its diagnostics to the
  unit's log; its effects on the file scope to the spine. Everything else is
  the next window's.

### The chain

The chain is today's stages, entered for a range. Each step below is one
stage's entry point taking a window's range where it takes the unit's now;
the one-lane emission's parts and the deferred reader's bodies are where two
of them exist already.

1. **Tokens.** The window's tokens: a slice of the preprocessor's output
   while that is made for the whole unit first, and later what the
   preprocessor yields (below).
2. **Tree.** The parser's passes over the window's tokens, into window
   columns. An external declaration is the parser's unit already: its roots
   are the root's children.
3. **Body.** For each function body of the window: the node machine where it
   takes the body; otherwise the reader's walks, the graph's construction,
   `check_graph_run` over the body's nodes, the warnings' passes,
   `check_program`'s statements and controls, and `check_lower_part`, each
   over the body's range, with the body's records in the window's arena.
4. **Code.** `tier0_compile`, the branches' sizes, `x86_encode` and the
   unwind rows for the body, as now, and its bytes appended to the unit's.

A stage that passes over the unit for a sparse construct (the per-token
pre-passes of `check_input_run`, the selection's walk, the unsequenced and
the uninitialized scans) passes over the window instead. A step that costs
time in the unit's length at each window would make the compile quadratic;
the oracle can't see that, so each step's gate counts instructions a token
against `W = ∞`.

### The front end by windows

- **The parser yields external declarations.** Its vector passes run over a
  buffer of tokens that ends at a candidate boundary: a `;` or a `}` at brace
  depth zero (the depth is a prefix sum over the kinds, 64 tokens a step).
  The roles then say whether the last root is complete (a body closed, a
  declaration ended); if it isn't (`struct S { … } s;`, a declarator after a
  brace), the buffer is extended to the next candidate and the tail parsed
  again. A window is the roots up to W tokens.
- **The preprocessor yields tokens.** Its main loop is a loop over contexts
  already; it stops at a candidate boundary of its output past W tokens when
  no macro's expansion is open, and resumes there. Its output is then a
  buffer of one window, not 12.5 MB of the unit. The unit's own file is lexed
  and refined in blocks of source ahead of the loop, with the lexer's carries
  across a block's edge, so that a 9.5 MB file's token columns are a block
  long too. Headers, which are small and shared by a lane's units, stay as
  they are.
- **Small units don't yield.** A unit under a few windows (most of musl's)
  is one window: its front end runs whole, and what
  [attested declarations](attested-declarations.md) needs before its file
  scope is read (the unit's declarations' hashes and the reach from its
  bodies) is there. In a unit of many windows the reach needs the names that
  later windows spell, so that design's passes run on the preprocessor's
  output before the chain starts, or its skip is left to units of one
  window; it is a rule for commands of many small units, and those are one
  window each.

### The unit's facts: the epilogue

A window's body can't know what the rest of the unit says about it. Each such
fact is either decided before the chain from the tokens alone, or left to the
epilogue, which reads only the windows' logs:

| fact | where |
| --- | --- |
| whether an internal or inline function is emitted (`check_program_select_mode`) | A function whose name no token outside its own definition spells can't be selected: its body is checked and not lowered. Others are lowered when the chain reaches them; the epilogue runs the exact selection over the windows' reference lists and leaves the unselected functions' code out of the object. |
| an `always_inline` callee's stream in its callers (#1282, #1398) | Such callees are known from their declarations. Their typed streams are kept for the unit; a caller whose callee comes later in the unit keeps its own stream and takes its code when the callee's is there, in the epilogue at the latest. |
| `-Wunused-function`, `-Wunused-variable` at file scope, `-Wundefined-internal` | The windows' lists of the declarations they used; the epilogue's pass over the environment. |
| tentative definitions, a later declaration's attributes, aliases, `#pragma weak`, sections | The environment's final state, at the object's symbols: nothing of a body's code depends on them but its symbol's binding, which the object writer sets last. A redeclaration that changes how an earlier body should have been compiled (a `target` attribute after the definition) sends the unit to `W = ∞`. |
| the order of the diagnostics, the error limit and the warnings dropped after an error | Each window's diagnostics carry the stage and the token they are today's at; the epilogue merges the logs into today's order (`diagnostic_merge`) and applies the limit. |
| type numbers | A body's types are interned as its window meets them, so `TypeId`s differ from `W = ∞`'s. #2524 already requires that nothing emitted read them; `-g`'s types are added as declarations reference them, which is source order in both. |
| string literals' and constants' merging, static data | By content, in the order of their first use: a window's are logged with their tokens and merged in the epilogue in the unit's order. |

A unit that the chain can't finish (an error that source order recovers from
differently, a construct in this table's last resort) is compiled again with
`W = ∞`, as the deferred reader repeats source order after a merge it can't
make. Its diagnostics are in its log until then, so nothing was printed.

### The twin, the oracle and the checks

- **`W = ∞` is the twin**, and `--scalar` keeps its own: every stage's scalar
  path runs under every W. The comparison is the driver's: objects byte for
  byte and diagnostics line for line, for W of 1 token (every declaration a
  window), 2,048, and ∞, on the vector and the scalar paths.
- **The corpora**: `test-checker-corpus`, `sqlite3.c`, Lua, musl,
  c-testsuite and `main.c`, each at the three W, with `-g`, and with the
  arenas poisoned between windows (every byte a window left is overwritten
  before the next, so that a stage that read what it didn't write shows).
- **`test-check-units` and `test-check-lanes`** already require a unit's
  object to be the same at every lane count; windows on lanes add W to their
  matrix.
- **Generated units** for the table above: a unit whose every function is
  used before its definition, redeclared after it, `always_inline` and called
  from an earlier body, or unused.

### Data-parallel first: what is vector and what is ordered

The chain adds no scalar loop: it calls the stages' own passes on shorter
ranges, and a pass that is vector over a unit is vector over a window. What
it adds is:

- **The windows' cut:** brace depth by a prefix sum over the kinds' masks,
  the candidates by a compare, 64 tokens a step; its scalar twin counts
  braces.
- **The copies out of a window:** the code's bytes and the rows, by ranges.
- **The clears:** a window's columns, by ranges.
- **The epilogue's merges:** the diagnostics' and the constants', by sorted
  keys.

A short window loses what a long pass gains from its width: a pass over 64
tokens at a time has a tail for each window, and the parser's passes have a
fixed cost a call. W = 2,048 tokens is 32 steps of 64, so a tail is a few
percent of a pass; step 1 measures the instructions a token at W against
`W = ∞`, and a stage whose passes can't run on a body of 200 tokens without
losing more than they gain takes the window, not the body, as its range.

What stays ordered is the spine, as C requires, and the epilogue's merges.
The spine is measured: the unit without its bodies is 18.9 ms of the unit's
116.9 alone and 36.7 of 245.8 at eight workers, and within it the reader's
walk of the declarations is the ordered step (`check-input`, 11.6 and 22.0
ms), which [attested declarations](attested-declarations.md) shortens for
commands of many units and step 4 keeps in the cache for one. The epilogue
isn't built, so it has no measurement; what it replaces is today's selection
and the object's stages, 2.5 ms alone and 5.5 at eight workers.
The chain's bodies are independent, which is the parallelism the lanes take.

### Lanes, units and processes

- **One unit on several lanes.** The spine runs on one lane; the others take
  windows behind it. This replaces two mechanisms that cut the unit
  differently: the emission's parts (#151) and the deferred reader's bodies
  (#113). Today one file's lanes run a sixth of the compile, the lowering and
  the emission; with the chain on lanes, what stays serial is the spine, a
  sixth of `sqlite3.c`'s time.
- **Several units on lanes.** A lane's window arena serves every unit it
  takes. None of it is given back between units, so the discards, the
  faults and the TLB flushes that
  [#1213](https://github.com/buster14a/buster/issues/1213) counts for the
  records of a unit's bodies don't happen; what a unit still returns is its
  environment and its output.
- **Processes.** Eight processes of one unit each share nothing but the
  machine, which is the case this design is measured on: each one's traffic
  falls, so the eight stop slowing each other.

## The steps and their gates

Each step is measured on the 9700X at one worker and at eight before the next
begins, and none becomes the default unless its gate holds. `W = ∞` keeps
today's path throughout, behind an option until the last step, then as the
tests' twin.

| step | what is built | gate, on `sqlite3.c` at eight workers unless it says | expected at eight workers |
| --- | --- | --- | ---: |
| 0. The gauge | The record's cohort runner in `tools/`, and a step of the 9700X job on main's pushes: a compile's cycles of both modes, its peak and its faults at one worker and at eight, by stage from the workers' own reports. No compile changes. | Reproduces this record's stage table within 3% at `4c8c3c9c`, in two holds of under 30 s. | a table |
| 1. The back half by parts | One lane's emission cuts the bodies into parts of W tokens, as the lanes' does, and takes each part's lowering and its code before the next part's; the writer's columns and a part's typed stream are reused; `always_inline` callees' streams are kept. | `lower` and the emission at most 1.25 times their one-worker user cycles (1.59 and 1.30 today). Instructions a token within 2% of `W = ∞`. Objects the same bytes on every corpus. One worker not slower. | 298 MB/s, 1.03 times |
| 2. The chain from the graph's run | `check_graph_run`, the warnings' passes, `check_program` and the references for a body's range, before step 1's parts; the stream's and the program's rows in the window's arena; the selection by names, exact in the epilogue. | `check-expressions` to `lower` at most 1.3 times their one-worker cycles (1.49 to 2.43). Those stages fault under 1,000 pages (12,673). | 336, 1.16 |
| 3. The chain from the reader | A body's reading joins the chain: its token columns and its graph are the window's, by biased bases, and it binds file-scope names through the environment at its position. With #2524's step 4, which needs the same reading. | The bodies' part of `check-input` at most 1.3 times its one-worker cycles (2.4 in time today). Peak RSS under 100 MB (190.7). Objects and diagnostics the same at W of 1, 2,048 and ∞, on both paths. | 441, 1.53 |
| 4. The tree and the file scope by windows | The parser yields external declarations; the spine reads a window's declarations into the environment before its bodies; the tree's columns are the window's. | The parse at most 1.3 times its one-worker cycles (3.43). The unit without its bodies at most 1.3 times (1.70). Peak under 60 MB. | 538, 1.86 |
| 5. The tokens by windows | The preprocessor yields; the unit's own file is lexed in blocks. | The preprocessor at most 1.2 times (1.40). Peak under 30 MB. Under 25 DRAM bytes moved a source byte (146). | 567, 1.96 |
| 6. Windows on lanes | The spine on one lane and the chain's windows on the others; a units' lane keeps its window arena across its units. | `--lanes 8` of `sqlite3.c` alone: the chain's stages scale as the emission's lanes do today. musl's command on 8 lanes: kernel cycles a pass at most eight processes' (1,417 M; 2,265 M today). | one unit's latency; musl's pass 1.1 |

**Across the three designs**, by what each next step gives eight workers on
`sqlite3.c` (the model's expected case, cumulative): the node machine's step
2, 395 MB/s; this design's steps 1 and 2 beside it, 439; the machine's step 3,
476; this design's step 3 with the machine's step 4, which are one piece of
work on the reader, 727; step 4, 1,032; step 5, 1,144. Step 0 comes first
because every later gate is its table, and step 1 early because it is the
cheapest test of the premise. Step 0's gauge reproduces this record's whole
compile and its stage table's times, user cycles, inflation and faults within
3%, but not the kernel cycles of the stages that have few, which the record's
own two measurements don't reproduce either ([its guide](commands/all-core.md#the-gate-at-4c8c3c9c)):
a later gate on one stage's kernel cycles needs a wider band. Attested declarations and
[#1213](https://github.com/buster14a/buster/issues/1213) are independent of
all of these and are musl's first two. Attested declarations' step 3 makes
the reader's unit-wide passes take the unit's read ranges where they take
every token; a window's range is the same change to the same passes, so
whichever of that step and this design's step 3 comes second finds it made.

## The model

The estimate is `tools/model.awk` of the record over its measured tables: a
compile of `sqlite3.c` at eight workers, stage by stage, after each step. Its
rules:

- **A stage in the chain costs its one-worker cycles times a residual.** The
  residual is what work that stays in its caches still pays beside seven
  others: 1.17 expected (a unit of musl as a process each is 1.19, Tier 0's
  and the encoder's own functions 1.21, #672's cluster 1.08 to 1.15), 1.30
  conservative, 1.08 optimistic. Only the optimistic case counts what cache
  residency gives a stage alone (10%), though #672's experiment found units
  of this size 37% dearer a token than small ones on one core.
- **The kernel's cycles follow the pages faulted**, at today's 4,400 cycles a
  page at eight workers. A chained stage faults its window's arena once (512
  pages expected), not its records for the unit.
- **In step 2 the graph is still read cold.** The reader wrote it for the
  whole unit, so half of the graph run's excess stays (a quarter to three
  quarters).
- **The node machine** removes, for the bodies it takes, the share of each
  stage's bodies' cycles that [its design](node-machine.md)'s steps name, at
  that design's coverage (70%, 85%, 95% of the body tokens) and cost (150,
  100, 60 cycles a body token for its three parts), which stays in the
  first-level cache (a residual of 1.10).
- **A compile's slot** is its cycles at 5.278 GHz plus the time it is off its
  CPU, 7.6 ms today, taken to follow the kernel's cycles.

| eight workers on `sqlite3.c`: MB/s, and times today's 289 | conservative | expected | optimistic |
| --- | ---: | ---: | ---: |
| this design's step 1 | 294, 1.02 | 298, 1.03 | 303, 1.05 |
| step 2 | 325, 1.12 | 336, 1.16 | 356, 1.23 |
| step 3 | 412, 1.43 | 441, 1.53 | 493, 1.71 |
| step 4 | 489, 1.69 | 538, 1.86 | 631, 2.19 |
| step 5 | 509, 1.76 | **567, 1.96** | 681, 2.36 |
| the node machine alone, after its steps 2, 3, 4 | 373, 400, 500 | 395, 448, 620 | 407, 485, 751 |
| both: step 2 here, the machine's step 2 | 405, 1.40 | 439, 1.52 | 468, 1.62 |
| step 2 here, the machine's step 3 | 425, 1.47 | 476, 1.65 | 517, 1.79 |
| step 3 here, the machine's step 4 | 602, 2.08 | 727, 2.52 | 859, 2.97 |
| step 4 here, the machine's step 4 | 781, 2.71 | **1,032, 3.57** | 1,392, 4.82 |
| step 5 here, the machine's step 4 | 834, 2.89 | 1,144, 3.96 | 1,660, 5.75 |

- **1 GB/s takes both.** It is 401 M cycles a compile. The windows alone leave
  708 M expected, because they remove no work; the node machine alone leaves
  628 M, because what it doesn't take still waits. Neither reaches 1 GB/s in
  its optimistic case. Together they leave 385 M at this design's step 4.
- **One core gains too, less.** A compile alone goes from 125 ms to 110 with
  the windows (the pages it no longer faults), to 63 with the node machine,
  and to 55 with both, by the same rules.
- **The bandwidth stops binding at step 3.** A compile then moves about 400
  MB through DRAM (the preprocessor's, the parser's and the file scope's
  records, by the stage table's last column), some 43 bytes a source byte,
  under #672's budget of 50; at step 5 it is the source, the environment and
  the object.
- **What the model can't say:** whether the second threads then add what they
  add to compute-bound work, which is more than today's 11%; and what a
  command of many small units gains, which is the table of designs above.

## What would refute it

- **Step 1** leaves the lowering's and the emission's waiting where it is:
  consumed hot or cold, the typed stream costs the same. The waiting is then
  not the boundaries', and the chain shouldn't be built further.
- **The chained stages keep more than 1.3 times their one-worker cycles** at
  eight workers once their records are a window's: something else is shared
  (the environment's lookups, the code itself, the frequency), and the
  model's residual is wrong.
- **Instructions a token rise by more than 5% at W = 2,048**: the stages'
  per-call costs or unit-long steps make the windows cost what they save.
- **The kernel's cycles don't follow the pages**: at eight workers a compile
  of a third of the peak costs more than half the kernel cycles, so the
  cost is in the faults' number or the locks, and reuse has to be judged by
  those.
- **A unit-wide fact that isn't in the table** changes a body's code, so that
  `W = ∞` is needed for more than rare units.

Each is a measurement that ends the work with a record.

## Risks and open questions

- **Two orders of one compiler.** Every stage has to be correct for a range
  and for the unit. The design's defence is that they are one code path (the
  unit is a window) and that the byte comparison at three W runs on every
  corpus; its cost is that every unit-wide assumption in a stage has to be
  found. The reader's are the hardest, and are #2524's step 4's work too.
- **The environment is shared ground.** #2524's step 4, attested
  declarations and this design all need a body to bind file-scope names
  through the environment at its own position. One of them builds it; this
  design's step 3 is written to take it, not to make another.
- **Quadratic steps.** Anything a stage does once a call in the unit's length
  (a `memset` of a column as long as the unit, a table's clear, a walk from
  the unit's first token) is invisible at `W = ∞` and ruinous at W = 2,048.
  The instruction gate of each step exists for this.
- **The largest bodies.** A body of 28,599 tokens is 8 MB of records in
  today's stages: it inflates as a unit of that size does (about 1.5 times)
  unless the node machine takes it. `sqlite3.c`'s ten largest bodies are
  15.5% of its body tokens.
- **Diagnostics.** A window's diagnostics are logged and merged, so a unit
  with errors prints them later than today, at its end. An error still stops
  what it stops today (the limit is applied in the merge), but
  `-Wfatal-errors` and the first error's latency change for a large unit.
- **`-g`.** The debug rows are per body and copy out of a window as the code
  does; the line table and the types are made in the epilogue from the logs.
  Not looked at beyond that.
- **The preprocessor's yield** is the least certain step: a macro's expansion
  that spans declarations, `#include` inside a declaration, `_Pragma` and the
  expansion history's replay for a diagnostic each need the loop's state kept
  across a yield. It is last, and its gate is its own.
- **Open:** whether W should be tokens or bytes of records; whether the
  window's arena is one arena or the stages' own arenas each popped; whether
  the chain on several lanes shares the caller's window arena or copies the
  environment; what `-Wfatal-errors` should do.

## What stays outside

- **A process's start and end**
  ([#1444](https://github.com/buster14a/buster/issues/1444)). Nothing for a
  unit of `sqlite3.c`'s size (0.3%); 42% of the cycles of musl's units as a
  process each one at a time, and 45% eight at a time. A build that gives a
  command many units pays it once.
- **Lanes against processes**
  ([#1213](https://github.com/buster14a/buster/issues/1213)). The lanes'
  kernel time is what one address space costs. Windows remove the part that
  is a body's records; a unit's own arenas (its preprocessor's, its
  environment's) are #1213's.
- **What a compile holds at its peak**
  ([#1701](https://github.com/buster14a/buster/issues/1701)). Every megabyte
  of peak costs 1.3 M kernel cycles a compile at eight workers. The chain
  removes the bodies' records from the peak; #1701's items are the rest
  until the front end yields.
- **The second threads.** Fifteen workers are 9% more than eight today,
  because the cores wait for memory either way. A compile that stays in its
  caches should gain what SMT gives compute, which is more; not modelled.
- **The host.** The memory's clock is 2,595 MHz (DDR5-5200; the modules are
  rated for 5600), transparent huge pages are `always`, and the kernel is
  6.18. Each is the owner's decision, and none is assumed changed.

## Related issues

- [#672](https://github.com/buster14a/buster/issues/672): the tracker, its
  model, [the windows' research](https://github.com/buster14a/buster/issues/672#issuecomment-6749355847)
  that proposed this order, and
  [the experiment](https://github.com/buster14a/buster/issues/672#issuecomment-6810078226)
  that didn't refute it.
- [#2524](https://github.com/buster14a/buster/issues/2524), the node machine,
  and [#2531](https://github.com/buster14a/buster/issues/2531), attested
  declarations: the two one-core designs this composes with.
- [#113](https://github.com/buster14a/buster/issues/113),
  [#151](https://github.com/buster14a/buster/issues/151): the deferred reader
  and the emission's lanes, whose body ranges and parts the chain joins.
- [#1213](https://github.com/buster14a/buster/issues/1213),
  [#1444](https://github.com/buster14a/buster/issues/1444),
  [#1701](https://github.com/buster14a/buster/issues/1701): the lanes' kernel
  time, a process's start, and the peak.
- [#1561](https://github.com/buster14a/buster/pull/1561): the page policy by
  the host's load.
