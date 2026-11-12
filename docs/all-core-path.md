# The all-core path: a compile whose working memory is a window of the unit (#2536): design

Status: design only. Nothing below is implemented, and its gains are a model's
that its first steps exist to confirm or refute. It is
[#672](https://github.com/buster14a/buster/issues/672)'s all-core target's
design: what eight workers cost a compile on the 9700X, what the designs
already in hand remove of that, and the change that removes the rest. The issue
is [#2536](https://github.com/buster14a/buster/issues/2536). The names of
existing code are main's at `4c8c3c9c`, and every figure not attributed
otherwise is that revision's on the 9700X, under the lock, from
[the measurement record](https://github.com/buster14a/buster-performance/tree/26d5707dddf0a1fb56418ad0e694b8a28a698265/docs/measurements/all-core-design-672-2026-11-12).

**In one paragraph.** Eight compiles of `sqlite3.c` at once cost each of them
1.89 times the cycles of one alone (1,297 M against 687 M), for the same
instructions: 295 MB/s where eight times one compile would be 594. All of the
difference is memory. Twice as many loads miss every cache, since the
third-level cache is shared by eight, and each waits a quarter longer: loads
that aren't complete are 390 M of the 433 M more user cycles. And the kernel's
work for the 190 MB of pages a compile touches once triples. The stages whose
loads reach all over the unit inflate most: the parse 3.0 times, `check-input`
1.8, `check-program` 1.8, the lowering 1.7. Tier 0 and the encoder, which stay
inside one function, cost 1.2 times. The design makes the rest of the compile
run as they do: the unit is read one **window** of external declarations at a
time, each window taken from its tokens to its code before the next is
touched, in memory that the next window reuses and that stays in the
second-level cache. Today's order is the same code with one window as long as
the unit: the twin, and the oracle. It composes with the two one-core
designs, [the node machine](node-machine.md) for the bodies and
[attested declarations](attested-declarations.md) for the file scope: they
remove work, and this removes the waiting for what is left. Expected for
eight workers on `sqlite3.c`: 1.9 times main's throughput from the windows
alone (558 MB/s), 2.1 from the node machine alone, and 3.5 to 3.8 from both,
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
| 1, CPU 2 | 74.3 | 125.0 | 602.4 M | 84.4 M | 13.1 M | 236.7 MB | 7,461 | 5.54 |
| 1, CPU 5 | 74.1 | 125.9 | 602.8 M | 84.4 M | 13.1 M | 236.7 MB | 7,460 | 5.50 |
| 2 | 113.6 | 164.3 | 722.5 M | 164.0 M | 13.9 M | 190.5 MB | 63,081 | 5.51 |
| 4 | 198.9 | 183.4 | 802.4 M | 182.6 M | 15.1 M | 190.7 MB | 63,006 | 5.47 |
| 8 | 295.4 | 252.3 | 1,044.7 M | 251.8 M | 15.2 M | 190.7 MB | 62,996 | 5.21 |
| 15, every online CPU | 323.1 | 433.0 | 1,802.7 M | 392.3 M | 15.6 M | 190.7 MB | 62,994 | 5.14 |

Every compile retires the same 1,348.9 M user instructions. Eight workers give
3.98 times one worker's throughput; each compile costs 1.73 times the user
cycles and 2.98 times the kernel's, 1.89 times in all. The second thread of
each core adds 9%. CPU 2 alone runs the performance governor, and a compile
alone on a powersave core (CPU 5) is 0.8% slower; nothing here changed a host
setting. From two workers on, a compile finds the host busy when it starts and
takes base pages (#1561): that is why the peak and the faults change there.

A stray process held a core through the day (a test program left looping by
another session, not under the lock). It sat on the second thread of core 0
in nearly every cohort, so from four workers on, the medians leave out the
worker on CPU 0, whose compiles took 300 ms and more, and the rates take that
worker's as the others' mean. The record has where the stray was for every
cohort.

For 1 GB/s on eight cores a compile has 76 ms of a core, 396 M cycles at the
frequency eight workers run at. Today's 1,297 M is 3.3 times that.

### All of it is memory

The same cohorts under three other counter sets, per compile:

| | 1 worker | 8 workers |
| --- | ---: | ---: |
| user cycles | 601.1 M | 1,033.7 M |
| … with a load not complete at retirement | 248.3 M | 638.8 M |
| dispatch slots waiting for the backend | 39.7% | 59.1% |
| L1D demand fills: all; from L3; from DRAM | 11.34 M; 1.20 M; 1.43 M | 10.93 M; 0.77 M; 2.95 M |
| L2 fills from L3, user | 8.62 M | 2.50 M |
| L2 fills from DRAM: user; kernel | 7.97 M; 5.07 M | 10.16 M; 5.15 M |
| L3 lookups that miss; a miss's sampled latency | 59%; 115 ns | 84%; 146 ns |
| mispredicted branches | 3.62 M | 3.70 M |

- **The waiting is the inflation.** 390 M of the 433 M more user cycles have a
  load that isn't complete. The branches, the instructions and the first-level
  cache's misses are the same.
- **Twice the misses, each a quarter longer.** Alone, a compile has the 32 MB
  third-level cache to itself; beside seven others it has an eighth. The
  demand fills that come from DRAM double (1.43 M to 2.95 M) and the L3's own
  fall by a third. An L3 miss's sampled latency (`l3_xi_sampled_latency`, on
  every CPU) is 115 ns with one worker, 104 with two, 115 with four, 146 with
  eight and 160 with fifteen, against 82 on the idle host.
- **The kernel's part is the pages.** A compile touches 190 MB of fresh pages
  once: 63,000 faults, each with its clear, its charge and its place in the
  kernel's lists, and at its end as many pages given back. That is 155 M
  kernel cycles alone with base pages and 252 M beside seven others: 4,000
  cycles a page, 1.3 M for each megabyte of the peak. The exit alone, where
  eight processes give their pages back at once, is about 35 M of them, and
  `native_queued_spin_lock_slowpath` leads its teardown.

### By stage

Each compile's own `--time-stages` report gives the stages' times; the
counters by stage are
[the one-core record](https://github.com/buster14a/buster-performance/tree/5318c1ea0e3a39f22f01b2bcb837db112c2a8fa7/docs/measurements/one-core-design-672-2026-11-12)'s
method run on every worker's CPU at once (`perf record -a` of a leader-sampled group with
sample times, each sample put in the run and the stage its time falls in).
Per compile, one worker against eight:

| stage | ms | user cycles, M | times | with a load not complete, M | kernel cycles, M | demand fills from DRAM, thousands | pages faulted, 8 | DRAM bytes moved, MB |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| preprocess | 9.7 → 15.6 | 43.1 → 60.2 | 1.39 | 8.7 → 23.3 | 10.2 → 17.5 | 15 → 57 | 5,643 | 93 → 80 |
| parse | 7.3 → 23.0 | 22.9 → 69.4 | 3.03 | 3.9 → 44.3 | 17.0 → 41.7 | 12 → 204 | 10,430 | 178 → 141 |
| check-input | 46.1 → 101.8 | 219.0 → 401.8 | 1.84 | 94.7 → 244.6 | 33.9 → 115.6 | 762 → 1,138 | 33,433 | 602 → 558 |
| check-expressions | 11.5 → 20.5 | 58.4 → 85.3 | 1.46 | 19.9 → 42.9 | 4.9 → 15.5 | 38 → 119 | 6,650 | 98 → 111 |
| check-declarations, static-data | 2.5 → 4.1 | 12.6 → 17.0 | 1.35 | 7.1 → 11.6 | 1.0 → 3.3 | 53 → 59 | 549 | 37 → 27 |
| check-program | 23.9 → 45.5 | 128.4 → 226.6 | 1.76 | 68.7 → 157.5 | 2.2 → 6.3 | 380 → 747 | 2,546 | 208 → 251 |
| select | 1.1 → 2.7 | 5.0 → 11.4 | 2.30 | 2.5 → 8.3 | 0.9 → 2.3 | 16 → 60 | 782 | 13 → 16 |
| lower | 11.4 → 19.7 | 61.0 → 101.4 | 1.66 | 25.9 → 64.1 | 1.7 → 2.6 | 95 → 234 | 2,147 | 69 → 101 |
| the emission, Tier 0 to the code's atoms | 8.6 → 12.3 | 46.1 → 64.3 | 1.39 | 12.7 → 28.5 | 1.3 → 0.6 | 39 → 96 | 299 | 33 → 54 |
| the object | 1.4 → 2.5 | 4.5 → 5.4 | 1.19 | 1.2 → 2.7 | 3.8 → 8.1 | 7 → 17 | 37 | 11 → 10 |
| the process's start and end | 0.3 → 0.3 | 1.3 → 1.9 | | 0.4 → 0.7 | 7.4 → 38.2 | 1 → 2 | 392 | 22 → 25 |
| whole compile | 123.6 → 247.8 | 602.4 → 1,044.7 | 1.73 | 245.8 → 628.5 | 84.4 → 251.8 | 1,419 → 2,733 | 62,908 | 1,364 → 1,374 |

The emission's stages interleave function by function, so they are one row.
The user cycles are the sampled cohorts' scaled to the unsampled medians, and
the kernel's are less the sampling's own. The pages are base pages at eight
workers; alone, the kernel clears huge pages instead (7,461 faults).

The same compile's function bodies, by family of function (the unit whole
less the unit without its bodies, the one-core record's split and families),
in user cycles a body token:

| family | alone | at eight | times |
| --- | ---: | ---: | ---: |
| the parser and the tree's columns | 44.1 | 140.9 | 3.19 |
| libc's `memset`, `memmove`, `memcmp` | 26.8 | 50.9 | 1.90 |
| the program and its controls | 127.0 | 235.7 | 1.86 |
| the reader's other functions | 159.9 | 283.1 | 1.77 |
| the graph's construction | 99.4 | 174.3 | 1.75 |
| warnings | 124.2 | 198.1 | 1.59 |
| initializers and static data | 23.9 | 37.6 | 1.57 |
| the lowering | 93.1 | 141.8 | 1.52 |
| the C rules for a node | 29.6 | 39.9 | 1.35 |
| the graph run's loop | 53.8 | 64.9 | 1.21 |
| the emission: Tier 0, the encoder, branch sizing, unwinding | 74.5 | 90.0 | 1.21 |
| all of the bodies | 883 | 1,561 | 1.77 |

- **What inflates is a load the prefetchers can't guess, into a record
  longer than the cache.** The parser's passes scatter into arrays as long as
  the unit (`parse_tree_columns`, `parse_top_parents8`): 3.2 times. The
  reader's and the program's walks follow the tree and the graph: 1.8. The
  warnings and the lowering read the graph a node at a time: 1.5 to 1.6.
- **What doesn't is a pass that streams, or one that stays in a function.**
  The graph's run reads its nodes in order and writes the stream behind it,
  and costs 1.21 times. Tier 0 and the encoder take one function's typed
  entries to its code and touch nothing else: 1.21 too, and what they do wait
  for is the stream that the lowering wrote for the whole unit before them.
- **By stage, the kernel's cycles follow the pages** the stage is first to
  touch: `check-input` 116 M for 33,433 pages, the parse 42 M for 10,430.

### The page policy apart from the load

One worker and eight differ in two things, the load and the pages, so the
other two corners were run too (`BUSTER_HUGE_PAGES=0` alone, `=1` at eight):

| | wall ms | user cycles | kernel cycles | load not complete | DRAM bytes moved | MB/s |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 1 worker, huge pages (main) | 125.0 | 602.4 M | 84.4 M | 248.3 M | 1,388 MB | 74.3 |
| 1 worker, base pages | 150.2 | 659.9 M | 155.1 M | 303.2 M | 1,150 to 1,310 MB | 62.5 |
| 8 workers, base pages (main) | 252.3 | 1,044.7 M | 251.8 M | 638.8 M | 1,385 MB | 295.4 |
| 8 workers, huge pages | 282.7 | 1,206.3 M | 276.6 M | 791.7 M | 1,727 MB | 272.1 |

With the pages held the same, eight workers cost 1.58 times the user cycles
and 1.62 times the kernel's in base pages, and 2.00 and 3.28 times in huge
pages. #1561's policy is still right at this main: 8.6% more throughput at
eight workers, where base pages alone would cost one compile 20%. Neither
corner is the fix: the inflation is 1.6 times with the better pages.

### DRAM traffic

By the memory controllers' counters (`amd_umc` read and write CAS commands, 64
bytes each, on every CPU, less a 2 s idle control; #672's claim holds physical
traffic conditional on
[buster-performance#49](https://github.com/buster14a/buster-performance/issues/49),
and so does this):

| workers | read, MB a compile | written | bytes a source byte | GB/s over the cohort | the data bus in use |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | 887 | 495 | 146 | 10.6 | 13% |
| 2 | 907 | 379 | 135 | 15.5 | 18% |
| 4 | 971 | 399 | 144 | 24.0 | 28% |
| 8 | 978 | 406 | 146 | 38.2 | 46% |
| 15 | 999 | 418 | 149 | 37.8 | 45% |

A compile moves 1.39 GB through DRAM whether it runs alone or beside seven
others: 146 bytes for each source byte (#672's first record had 269, and
217 two days later). Eight at once would ask the bus for eight times one
compile's rate, 89 GB/s, where two channels of DDR5-5200 carry 83.2 at most;
they get 43 GB/s while all eight run (the median of the counters read every
2 ms is 41, and the cohort's own mean, which the slowed worker stretches, 38;
fifteen workers' is 42 in the rounds where no worker shared its CPU with the
stray), and slow down until their traffic fits. So #672's model still holds: 43 GB/s
over 146 bytes a byte is 295 MB/s. Streams over 1 GB a worker on the same
eight cores, the same day, move 55 GB/s reading, 52 with ordinary stores and
56 copying (one core alone: 48, 46 and 51): the compiles use three quarters
of what a stream gets.

By stage, per compile at eight workers (each 2 ms interval's count divided
among the stages its runs were in; the stage table's last column):
`check-input` 558 MB, `check-program` 251, the parse 141, `check-expressions`
111, the lowering 101, the preprocessor 80, the whole emission 54.

### The bodies and the file scope at eight workers

The unit preprocessed, compiled whole and with each of its 2,674 function
bodies replaced by `;` (the one-core record's split), at one worker and at
eight:

| per compile | whole, 1 | file scope, 1 | bodies, 1 | whole, 8 | file scope, 8 | bodies, 8 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| wall ms | 117.2 | 18.9 | 98.3 | 243.6 | 36.2 | 207.4 |
| user cycles | 563.3 M | 74.3 M | 489.0 M | 1,006.0 M | 123.2 M | 882.8 M |
| kernel cycles | 80.3 M | 26.4 M | 53.9 M | 247.1 M | 52.7 M | 194.4 M |
| load not complete | 239.1 M | 19.3 M | 219.8 M | 636.1 M | 61.3 M | 574.7 M |
| peak RSS | 228.5 MB | 84.9 MB | | 180.5 MB | 43.8 MB | |
| DRAM bytes moved | 1,344 MB | 223 MB | 1,121 MB | 1,364 MB | 182 MB | 1,182 MB |

The bodies are 86% of a compile's cycles at eight workers, and 84% alone.
By stage, of the compile's 116.1 ms alone and 240.0 ms at eight workers: the
bodies' part of the stages between the parser and Tier 0 (`check-input` to
`lower`, the node machine's territory) is 81.3 and 166.2 ms; of the parse,
5.6 and 19.0; of Tier 0 and the emission, 8.4 and 12.4. The file scope alone, a sixth of the
unit's time, still inflates 1.66 times in user cycles: 150,169 tokens of
declarations are 44 MB of fresh pages, far past any cache.

### musl: units on lanes, units as processes

The frozen corpus's musl, the first source of each base name: 1,336 units,
5,039,561 tokens, 61% of them file scope. To objects five ways, per pass over
the units:

| way | pass ms | units/s | user cycles | kernel cycles | L2 fills from DRAM | faults |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| one command, one lane (CPU 2) | 1,072 | 1,246 | 4,816 M | 1,101 M | 10.3 M | 343,109 |
| one command, 8 lanes | 222 | 6,016 | 5,812 M | 2,295 M | 47.8 M | 326,797 |
| 8 commands, a share of the units and one lane each | 218 | 6,131 | 5,479 M | 1,501 M | 39.1 M | 392,180 |
| a process a unit, one at a time | 2,300 | 581 | 5,894 M | 4,319 M | 44.8 M | 855,089 |
| a process a unit, 8 at a time | 457 | 2,922 | 7,033 M | 5,621 M | 91.5 M | 1,777,589 |

- **Eight lanes are 4.8 times one lane**, for 1.37 times the cycles: the user
  cycles rise 1.21 times and the kernel's 2.08. A pass moves 1.5 GB through
  DRAM on one lane and 4.9 GB on eight.
- **The user side barely inflates.** By family of function, eight lanes
  against one: the reader 1.08 times, the parser 1.08, the graph's
  construction 1.13, the program 1.13, the emission 1.05, the preprocessor
  1.24, `memset` 1.36. A unit of musl is 3,800 tokens: its records are in the
  caches already. A unit's own compile as a process each costs 1.19 times the
  user cycles at eight at a time; the cluster of 2,000 tokens of
  [#672's experiment](https://github.com/buster14a/buster/issues/672#issuecomment-6810078226)
  cost 1.08 to 1.15 times, and a unit of `sqlite3.c`'s size 1.84.
- **Lanes against processes.** The same units as eight commands take the same
  time (a pass is its slowest share's) for 14% fewer cycles: 0.94 times the
  lanes' user cycles and 0.65 times their kernel cycles. What the lanes pay
  for one address space is 800 M kernel cycles a pass: the TLB flushes sent
  to the other lanes when a unit's arenas are given back are 660 M sampled
  cycles a pass on eight lanes and 14 M as eight processes, and the pages
  come back to be cleared cold (183 M against 88 M)
  ([#1213](https://github.com/buster14a/buster/issues/1213)).
- **A process a unit** costs 1.73 times the cycles of one command alone and
  2.14 times at eight at a time, and takes 2.1 times as long as the command
  on lanes: 3.2 M cycles of start and end for a unit whose own compile is
  4.4 M ([#1444](https://github.com/buster14a/buster/issues/1444)).

### What the designs in hand remove at eight workers

Each as a share of what a compile costs at eight workers (1,297 M cycles for
`sqlite3.c`, 8,107 M a pass for musl's command on eight lanes): measured where
it could be, and by [the model](#the-model) where it is a design's estimate.

| design | `sqlite3.c`, eight processes | musl's command, eight lanes |
| --- | --- | --- |
| **The node machine**, [#2524](https://github.com/buster14a/buster/issues/2524): the bodies between the parser and Tier 0 | Its territory is 878 M of the 1,297 M, 68% (64% alone): 3.1 times if it were free. The cycles a body token that its steps 2, 3 and 4 stop spending are 605, 294 and 334 at eight workers, where they are 384, 171 and 187 alone. With its own coverage and cost: **1.37, 1.56 and 2.15 times** after those steps (405, 460 and 634 MB/s). | The families it removes inflate 1.1 to 1.25 times on lanes, as the whole pass's user side does: about 1.1 times, as alone. |
| **Attested declarations**, [#2531](https://github.com/buster14a/buster/issues/2531): a file-scope declaration read once a command | Nothing: a command of one unit has no memo. | The reader's functions are 1,615 M of a pass's 5,694 M sampled user cycles and inflate 1.08 times on lanes, where the pass's cycles inflate 1.37: the file scope's reading falls from 32% of the command to about a quarter, and the 1,099 M cycles a pass that its skip measures alone are about 1,250 M of the lanes' 8,107 M. **About 1.2 times** by that estimate, where the skip measures 1.25 alone; it wasn't run on lanes. |
| **The windows**, [#672's research](https://github.com/buster14a/buster/issues/672#issuecomment-6749355847): this design | 588 M of the 1,297 M after its five steps, expected: **1.89 times** (558 MB/s). | Little on the user side: the units are a window each already. The bodies' records leave the arenas a unit gives back. |
| **The lanes' kernel time**, [#1213](https://github.com/buster14a/buster/issues/1213) | Nothing: no lanes. | 794 M kernel cycles and 333 M user cycles a pass against the same units as eight processes: **1.16 times**. |
| **A process's start**, [#1444](https://github.com/buster14a/buster/issues/1444) | 0.3%. | Nothing in one command. Of a process a unit, eight at a time: 45% of 12,654 M cycles. |
| **The peak**, [#1701](https://github.com/buster14a/buster/issues/1701) | 1.3 M kernel cycles a megabyte: the 190.7 MB are 252 M, a fifth of the compile. | The kernel is 2,295 M of a pass's 8,107 M, 28%. |
| The asm matcher's index, [#2532](https://github.com/buster14a/buster/issues/2532) | Nothing: no asm statement. | 7.9% of the command alone (its issue); not measured on lanes. |

So at eight workers the node machine ranks first for `sqlite3.c` (2.15 times
after its last step), the windows second (1.89), and no design in hand removes
what the other leaves: the machine takes the bodies' middle out of both the
waiting and the pages, and what stays (the parse, the file scope, the
preprocessor, the bodies it refuses, the emission) is 603 M cycles a compile
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
those steps reads what the one before wrote microseconds ago. It moves 54 MB
of the compile's 1,380, and inflates 1.2 to 1.4 times, most of that for the
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
  kernel's work for 190 MB of pages is 252 M cycles a compile at eight
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
| 1. The back half by parts | One lane's emission cuts the bodies into parts of W tokens, as the lanes' does, and takes each part's lowering and its code before the next part's; the writer's columns and a part's typed stream are reused; `always_inline` callees' streams are kept. | `lower` and the emission at most 1.25 times their one-worker user cycles (1.66 and 1.39 today). Instructions a token within 2% of `W = ∞`. Objects the same bytes on every corpus. One worker not slower. | 307 MB/s, 1.04 times |
| 2. The chain from the graph's run | `check_graph_run`, the warnings' passes, `check_program` and the references for a body's range, before step 1's parts; the stream's and the program's rows in the window's arena; the selection by names, exact in the epilogue. | `check-expressions` to `lower` at most 1.3 times their one-worker cycles (1.35 to 2.30). Those stages fault under 1,000 pages (12,674). | 345, 1.17 |
| 3. The chain from the reader | A body's reading joins the chain: its token columns and its graph are the window's, by biased bases, and it binds file-scope names through the environment at its position. With #2524's step 4, which needs the same reading. | The bodies' part of `check-input` at most 1.3 times its one-worker cycles (2.3 in time today). Peak RSS under 100 MB (190.7). Objects and diagnostics the same at W of 1, 2,048 and ∞, on both paths. | 444, 1.50 |
| 4. The tree and the file scope by windows | The parser yields external declarations; the spine reads a window's declarations into the environment before its bodies; the tree's columns are the window's. | The parse at most 1.3 times its one-worker cycles (3.03). The unit without its bodies at most 1.3 times (1.66). Peak under 60 MB. | 531, 1.80 |
| 5. The tokens by windows | The preprocessor yields; the unit's own file is lexed in blocks. | The preprocessor at most 1.2 times (1.39). Peak under 30 MB. Under 25 DRAM bytes moved a source byte (146). | 558, 1.89 |
| 6. Windows on lanes | The spine on one lane and the chain's windows on the others; a units' lane keeps its window arena across its units. | `--lanes 8` of `sqlite3.c` alone: the chain's stages scale as the emission's lanes do today. musl's command on 8 lanes: kernel cycles a pass at most eight processes' (1,501 M; 2,295 M today). | one unit's latency; musl's pass 1.1 |

**Across the three designs**, by what each next step gives eight workers on
`sqlite3.c` (the model's expected case, cumulative): the node machine's step
2, 405 MB/s; this design's steps 1 and 2 beside it, 451; the machine's step 3,
491; this design's step 3 with the machine's step 4, which are one piece of
work on the reader, 743; step 4, 1,020; step 5, 1,125. Step 0 comes first
because every later gate is its table, and step 1 early because it is the
cheapest test of the premise. Attested declarations and
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
- **The kernel's cycles follow the pages faulted**, at today's 4,000 cycles a
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
- **A compile's slot** is its cycles at 5.205 GHz plus the time it is off its
  CPU, 8.4 ms today, taken to follow the kernel's cycles.

| eight workers on `sqlite3.c`: MB/s, and times today's 295 | conservative | expected | optimistic |
| --- | ---: | ---: | ---: |
| this design's step 1 | 303, 1.03 | 307, 1.04 | 312, 1.06 |
| step 2 | 333, 1.13 | 345, 1.17 | 366, 1.24 |
| step 3 | 415, 1.40 | 444, 1.50 | 498, 1.68 |
| step 4 | 483, 1.63 | 531, 1.80 | 623, 2.11 |
| step 5 | 501, 1.70 | **558, 1.89** | 671, 2.27 |
| the node machine alone, after its steps 2, 3, 4 | 382, 411, 510 | 405, 460, 634 | 417, 499, 768 |
| both: step 2 here, the machine's step 2 | 415, 1.41 | 451, 1.53 | 482, 1.63 |
| step 2 here, the machine's step 3 | 437, 1.48 | 491, 1.66 | 535, 1.81 |
| step 3 here, the machine's step 4 | 611, 2.07 | 743, 2.51 | 884, 2.99 |
| step 4 here, the machine's step 4 | 772, 2.61 | **1,020, 3.45** | 1,376, 4.66 |
| step 5 here, the machine's step 4 | 821, 2.78 | 1,125, 3.81 | 1,629, 5.52 |

- **1 GB/s takes both.** It is 396 M cycles a compile. The windows alone leave
  709 M expected, because they remove no work; the node machine alone leaves
  603 M, because what it doesn't take still waits. Neither reaches 1 GB/s in
  its optimistic case. Together they leave 384 M at this design's step 4.
- **One core gains too, less.** A compile alone goes from 125 ms to 110 with
  the windows (the pages it no longer faults), to 63 with the node machine,
  and to 55 with both, by the same rules.
- **The bandwidth stops binding at step 3.** A compile then moves about 400
  MB through DRAM (the preprocessor's, the parser's and the file scope's
  records, by the stage table's last column), some 43 bytes a source byte,
  under #672's budget of 50; at step 5 it is the source, the environment and
  the object.
- **What the model can't say:** whether the second threads then add what they
  add to compute-bound work, which is more than today's 9%; and what a
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
