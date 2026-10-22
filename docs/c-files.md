# Per-file jobs, cached process-wide

`src/buster/compiler/c_files.c` runs a file's preparation (lexing, refinement
and the directive table) as a job of its own, many files at once on a group of
lanes (`lane.c`), into a file cache that every translation unit preprocessing
with it shares. It also runs those jobs ahead of the preprocessor along the
files' literal includes, so that the units find their files ready. This is
[#150](https://github.com/buster14a/buster/issues/150), part of
[#149](https://github.com/buster14a/buster/issues/149) (#15's "Multicore"),
on #119's file cache, #116's refinement and #118's directive tables. This
document records the design, how it is checked, and its measurements on the
Ryzen 7 9700X.

## Results

**The same output on every lane count.** `test-files` fills a cache on 1, 2, 3
and 8 lanes and requires the same results byte for byte: the shared intern
table's ids and spellings, every file's tokens, values, leading-space bits,
directive table and guard, what the walk did, and each translation unit's
output stream (kinds, flags, values and locations) when it preprocesses with
the cache. Each unit's spellings are also those it gives with a cache of its own,
as the preprocessor runs alone. On the 9700X and locally this holds for the
test's tree and generated spellings, `sqlite3.c`, Lua 5.4.8's 34 units, this
repository's 18 units, and, prepared without their includes, the C library
corpus (8,983 files).

**Speed**, measured on 2026-10-22 on the 9700X (clang 23.1.1, `build.sh`'s
`-O3 -march=native`), under the `exclusive.sh` lock, lane i on CPU i, by
`benchmark-files`: medians of 11 runs, each from nothing (a new intern table
and cache, and every file opened, mapped, lexed, refined and indexed):

| Lanes | C library corpus, flat | `sqlite3.c` and its headers | musl, 1,357 units | This repository, 18 units |
| ---: | ---: | ---: | ---: | ---: |
| 1 | 79.18 ms | 7.48 ms | 5.03 ms | 5.42 ms |
| 2 | 63.37 ms (1.25×) | 7.48 ms | 4.24 ms (1.19×) | 5.19 ms |
| 3 | 49.99 ms (1.58×) | 7.33 ms | **4.14 ms (1.21×)** | **5.15 ms (1.05×)** |
| 4 | 45.12 ms (1.75×) | **7.33 ms (1.02×)** | 4.17 ms | 5.18 ms |
| 5 | 43.04 ms (1.84×) | 7.37 ms | 4.19 ms | 5.18 ms |
| 6 | **42.35 ms (1.87×)** | 7.39 ms | 4.24 ms | 5.21 ms |
| 7 | 43.16 ms (1.83×) | 7.45 ms | 4.28 ms | 5.28 ms |
| 8 | 43.68 ms (1.81×) | 7.60 ms | 4.32 ms | 5.35 ms |

What a run does:

| Roots | Rounds | Files | Bytes | Tokens | Shared ids | Opens |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| The C library corpus (Zig 0.14.1 `lib/libc`), without includes | 1 | 8,983 | 93.8 MB | 13,152,141 | 597,638 | 8,983 |
| `sqlite3.c` 3.53 | 8 | 420 | 15.1 MB | 1,665,617 | 40,093 | 1,085 |
| musl 1.2.5's sources, with its build's options | 4 | 1,598 | 2.4 MB | 580,440 | 15,026 | 2,535 |
| This repository's translation units, with `-march=native` | 7 | 433 | 10.0 MB | 1,290,059 | 35,996 | 1,295 |

- **The C library corpus** is the one large round: 94 MB prepared by every
  lane, 1.87 times as fast on 6 lanes as on one, 310 million tokens a second.
  It scales no further because eight lanes are bound by memory: their IPC falls
  from 2.06 on one lane to 0.77, while the clock falls only from 5.53 to 5.07
  GHz (`perf stat` of whole runs). "Where the time goes" has the phases.
- **The walks** are made of rounds below 4 MB per lane (`BUSTER_FILES_LANE_BYTES`),
  which one lane prepares while every lane probes paths: musl's 2,535 probes
  take 1.34 ms on 3 lanes against 2.70 ms on one, the 1.21 times. Round 0 of
  `sqlite3.c`'s walk is `sqlite3.c` itself, 9.5 MB that one lane lexes and
  refines in 3.8 ms; its headers then take 3.7 ms on one lane and 3.4 ms on 4.
  Forcing every round onto every lane (`--lane-bytes 0`) makes that walk 8% to
  19% slower (8.09 ms on 2 lanes, 8.90 ms on 8), since a lane that joins pays
  for fresh memory and the merge.

**Speculation.** The walk follows every literal include, also those under an
`#if` that turns out to be false. What it prepared that no unit entered:

| Roots | Files prepared | Unused | Their tokens |
| --- | ---: | ---: | ---: |
| `sqlite3.c` | 420 | 250 | 423,621 of 1,665,617 (25.4%) |
| musl's sources | 1,598 | 0 | 0 |
| This repository's units | 434 | 42 | 18,793 of 1,292,071 (1.5%) |

`sqlite3.c`'s unused tokens are 85% of its headers' 497,487: clang's AVX-512
intrinsic headers (`avx512fintrin.h` alone is 64,533 tokens), which an
`#include <immintrin.h>` under an `#if` that is false here reaches, and ICU's
`urename.h` (`SQLITE_ENABLE_ICU`). The repository's are glibc's 32-bit and
fortified headers. musl's units enter every header the walk reaches.

## Design

### The cache

`FileCache` (`c_preprocessor.c`) is #119's file cache made process-wide:

- **A record per file identity** (device and inode): the file's bytes and,
  once prepared, its tokens, leading-space bits, directive table and guard. A
  record never changes after it is prepared, so any number of translation
  units and lanes read it at once.
- **A path entry per path looked up:** the path's record, or none (no file, a
  directory, a file too big or unreadable). A translation unit's include
  search then makes no system call for a path the cache already looked up.
- **A translation unit keeps its own state about each file:** `PPFile` (how a
  path was found, its system level) and `PPFileData`, which copies the record's
  prepared fields when the unit first enters the file and keeps `once` (`#pragma
  once`), the line cursor and whether the unit has entered the file. So guards
  and `#pragma once` work per unit as before, and a unit without a cache of
  its own (`PPOptions.files` null) makes one, so every earlier command runs as
  before: `-E`'s text is byte-identical, and `test-preprocessor --clang`
  passes.
- **System calls in one place:** `file_cache_probe` opens a path, reads its
  identity, and unless a record has that identity, maps or reads its bytes.
  `FileCacheCounts` counts its opens, mappings and reads for whoever called
  it: a unit's `Preprocessor.probes`, or a run's lanes.

### Per-file jobs on lanes

A round of files is prepared by `files_prepare`, which every lane of the group
calls. The files are in the walk's order (the order one lane prepares them
in), and rule 1's costs are their bytes. The lanes that work on a round are
one per `BUSTER_FILES_LANE_BYTES` of its bytes, up to all of them:

- **One lane** prepares the files one after another straight into the shared
  intern table (`pp_prepare_into`, as a translation unit would).
- **Several** each prepare their files into an intern table of their own, and
  then merge (below). With all the group's lanes and enough bytes per lane, the
  lanes take rule 1's parts from a counter; with fewer, or few bytes per lane,
  each working lane takes one range (`range_part_by_cost`).

A lane that joins a round pays for the fresh memory of its own arenas and
table, whose first touches the kernel clears, and the merge costs about 1 ms on
a round of 30,000 new spellings. On the 9700X a round pays that back from a few
megabytes of source per lane; "Where the time goes" has the measurements.

### Interning from several lanes

Interning is the one part of preparing a file that isn't the file's alone:
every identifier gets an id in the shared table, and the same spelling must
get the same id in every file. #150 named three ways to intern from several
lanes:

1. each lane interns into a table of its own, and the tables are merged after
   the phase, renumbering the lanes' tokens;
2. rule 4's exchange (`docs/object-writer-and-linker.md`, "Lanes"): the lanes
   partition their identifiers by hash, and each interns the partitions it
   owns, in input order;
3. a concurrent table with atomic inserts.

The ids must not depend on the lane count, so that a translation unit's output
is the same however its files were prepared (#153). The design takes the first
and does its merge with the second's exchange, with ids in the order of first
occurrences:

- Each lane refines its files with `refine_tokens`, unchanged, into a table of
  its own, whose first ids are the predefined names, as the shared table's are.
  A lane prepares its files in order (it takes parts by `lane_take`'s numbers,
  not by `lane_parts_by_cost`'s costliest-first order), so its new spellings get
  ids in the order of their first occurrences in its files.
- The merge gives each spelling new to the shared table its id there in the
  order of its first occurrence in the round's files: by file, then by token.
  That is the order refining the files one after another into the shared table
  gives, so one lane, which does exactly that, is the merge's scalar twin, and
  `test-files` requires every lane count to give its ids.

The merge, in `files_prepare`, with a sync between steps:

1. **Lookup.** Each lane looks its round's new spellings up in the shared table
   (read-only while lanes read it). Those there get their ids; the others are
   candidates, counted by partition of their hashes.
2. **Exchange.** Each lane places its candidates in the partitions' array,
   after every partition before theirs and, within one, every lane before it.
   A candidate carries the spelling's first 16 bytes, its hash, and a key that
   orders occurrences: its file, its lane-local id, and its lane.
3. **First occurrences.** Each lane takes a range of partitions, and through a
   table of each partition's spellings keeps, for every distinct spelling, the
   candidate with the lowest key.
4. **Counts.** Each lane reads which of its candidates are first occurrences,
   and counts them and their bytes per file; lane 0 sums the counts in file
   order, which gives each file its first new id and record offset, and makes
   room in the shared table for all of them at once (`intern_bulk_begin`).
5. **Additions.** Each lane adds its first occurrences with their ids
   (`intern_bulk_add`; if the table grew, its old spellings are placed again
   too, a range of ids per lane), then takes every other candidate's id from
   its spelling's first.
6. **Rewrite.** Each lane rewrites its files' identifier values, and their
   guards, from its own ids to the shared table's (`files_remap`, 64 tokens at a
   time: one compare finds the identifiers).

A lane writes only its own spellings and its own segments of the partitions,
and reads the others' through the candidates (rule 2). The shared table's
slots are the one place lanes write at once: a lane takes an empty slot by
exchanging its id, 0, for the spelling's (`intern_place_shared`, a `lock
cmpxchg`), along the same probe sequence as `intern_place`, so the slots a
probe passes stay full and lookups find what they did.

The other two ways, against this one:

- **The exchange of occurrences** (way 2 alone) would move every identifier
  occurrence through the partitions instead of each lane's distinct spellings:
  6.1 million records for the C library corpus against 751,000 candidates on 8
  lanes, and its interning would still need the first occurrences' order.
- **A concurrent table** (way 3) gives ids in the order lanes happen to insert
  them, which changes from run to run. Making them independent of the lanes
  needs each spelling's first occurrence and a rewrite of every identifier:
  this merge's steps 3 to 6 again, after interning against one table that
  every lane writes, whose 16 MB of buckets for the C library corpus no lane's
  cache holds.

Both would need `c_refine.c`'s pass B to change (to emit occurrences, or to
insert concurrently), where the design here uses `refine_tokens` as it is. The
merge's measured cost bounds what either could save: on the C library corpus on
8 lanes, its four steps take 11.3 ms of the busiest lanes' time, against 21.4
ms for preparing ("Where the time goes").

### Running ahead

`files_run_ahead` is a frontier walk in rounds, every lane running it:

- **Round 0** prepares the roots (the units' main files), found as a unit finds
  its main file: by their paths.
- **Each later round** reads the literal header names in the new files'
  directive tables (`#include "x.h"`, `#include <x.h>`, `#include_next` and
  `#import`; a computed include can't be resolved early), searches for each by
  the preprocessor's own search (`pp_include_candidate`, from the path and
  search directory the includer was reached by, as `pp_find_include` does), and
  prepares the files it found, until a round finds none.
- **The searches advance in waves.** Requests with the same key (the same
  candidates: the includer's directory for a quoted name, the first search
  directory, the name) share one search. In a wave, each search goes through
  its candidates in order, answering from the cache where it can, until a path
  the cache never looked up; the wave's new paths are probed on every lane at
  once, then added to the cache on one lane. So each search ends where the
  preprocessor's will, and a unit that searches after the walk finds every path
  in the cache.
- **Bytes.** One lane maps the files it finds with `MAP_POPULATE`. Lanes that
  mapped files at once would queue on the process's memory map lock, which every
  mapping holds, so with more lanes each maps without it: the pages are faulted
  in as the lexer first reads them, which takes no process-wide lock.
- **Speculation stays invisible.** The walk prepares headers that sit under an
  inactive `#if`, which costs time but changes nothing: it runs no macro,
  takes no branch and reports nothing. A missing header is a path entry with
  no record, which a unit reports only if it reaches the include, and a file
  the walk couldn't prepare is left unprepared, for the unit that enters it to
  prepare and say why it can't.

### Memory

Every array a lane or a run writes that can grow is in an arena of its own,
whose first 64 KB are base pages (`lane.c`), so that one used for a few
kilobytes doesn't touch a huge page that the kernel clears whole (the
preprocessor's lesson: `docs/c-preprocessor.md`, "Where the time goes"). The
cache reserves each lane's arenas before the lanes start, since reserving takes
the memory map lock. Before the lanes write a fresh array they share (the
candidates, the shared table's new buckets, spans and records), each touches a
range of its pages (`files_touch`): a page that lanes fault at once is cleared
by one while the others wait. Each lane's own table starts at the size of its
share of its first round's bytes (`intern_initialize_with`).

## Where the time goes

The C library corpus without includes, on 1 and 8 lanes (ms, on the lane that
spent the most on each phase; medians of 21 runs):

| Phase | 1 lane | 8 lanes |
| --- | ---: | ---: |
| Preparing (lexing, refining, directive tables) | 58.95 | 21.43 |
| The merge's lookups | | 1.88 |
| The exchange and first occurrences | | 4.44 |
| Additions to the shared table, and taking ids | | 4.46 |
| Rewriting the files' identifiers | | 2.42 |
| Searches and probes (opening and mapping the files) | 18.68 | 7.13 |
| Work on one lane alone | 1.46 | 1.65 |
| Waiting at syncs | | 3.71 |
| The run | 79.18 | 43.68 |

The 9700X's profile of the 8-lane run (`perf record`, all samples): the kernel
clearing fresh pages (`clear_page_erms`) takes 32%; the lexer 13% and
refinement 16%. The clearing is the lanes' fresh memory: the lexer's token
columns (9 bytes a token, 10% of the samples), the candidates (4%), each lane's
own table, the shared table's new layout and records. One lane clears the
columns too, but eight lanes clearing at once compete for the same memory
bandwidth.

What the profiles changed, each measured on the 9700X (8 lanes, the C library
corpus, medians of 7 to 21 runs):

- **The memory map lock.** Mapping files with `MAP_POPULATE` from every lane
  took 16% of the samples in `osq_lock`, under `vm_mmap_pgoff`: each mapping
  holds the process's memory map lock. Lanes now map without it, and the pages
  are faulted in as the lexer reads them, which takes no process-wide lock:

  | How lanes get files' bytes | Searches and probes | The run |
  | --- | ---: | ---: |
  | Mapped with `MAP_POPULATE` | 12.94 ms | 50.16 ms |
  | Read into each lane's arena | 9.20 ms | 46.47 ms |
  | Mapped without it (the default) | 7.04 ms | 45.17 ms |

- **Fresh huge pages for small arrays.** The run's and each lane's arrays that
  grow first shared arenas, so a lane wrote a few kilobytes into a fresh 2 MB
  page the kernel cleared whole (`lane_parts_by_cost`'s order, 7.6% of the
  samples on `sqlite3.c`'s walk): each now has an arena of its own.
- **The merge read and wrote other lanes' memory at random**, and each `lock
  cmpxchg` into the shared table waited for a miss: the candidates now carry
  their keys, each lane pulls its own results from them, the shared table's
  buckets are prefetched for writing, and the lanes touch fresh shared arrays
  a range of pages each before writing them. The merge's steps went from 15.6
  to 13.2 ms (the busiest lanes').
- **Which lanes work.** Every lane on every round made the walks slower than
  one lane; one lane per 1 MB still left `sqlite3.c`'s walk at 8.72 ms on 8
  lanes, and the repository's at 6.09 ms. At 4 MB per lane (and 8 MB, the same)
  no walk is slower on more lanes than on one.

## Correctness

`./build/compiler test-files [--lanes LIST] [--seed N] [--keep] [--flat]
[options] [path...]` (about 0.3 s without paths) checks every check on each
lane count of `--lanes` (1, 2, 3 and 8), each time with a new intern table and
cache, against the first lane count's results, byte for byte, as "Results"
says, and every unit against itself run alone; and nothing may be written to
stderr while the cache is filled:

- **The tree:** three translation units and the headers they include in a
  directory of the test's own. It has includes by the includer's directory and
  through `-I` and `-isystem`, an `#include_next` chain through three
  directories, a header reached by four paths (a symbolic link and `..` among
  them), a hard link to a header a computed include also names (which the walk
  can't follow: the unit finds the file prepared under its other name), guarded
  and `#pragma once` headers included twice, a header under `#if 0` that exists
  (prepared but never entered) and one that doesn't (no diagnostic), and a
  missing header in an active line, which the unit reports. The counts are
  exact: 13 files prepared, each identity once, in 4 rounds; 1 unused; 2
  requests not found; and after the walk, the unit with the computed include
  opens one path and the others none. It runs with the vectorized kernels and
  their scalar twins, its rounds cut into parts and split once, all on every
  lane (its rounds are small enough for lane 0 alone otherwise).
- **Spellings:** 240 generated files of identifiers (a shared vocabulary drawn
  unevenly, names of their own, names past 16 and 63 bytes, line splices and
  UCNs), prepared without following includes. They make 165,562 ids, so the
  shared table grows during a merge. They run in parts, split once, and with
  two lanes working and the others idle, and in two runs into one cache, which
  must equal one run on one lane.
- **Files:** each path's `.c` and `.h` files as translation units with `-E`'s
  options, all the walk's roots; with `--flat`, only prepared.

Six mutations of the merge each fail it: taking the last occurrence instead of
the first, skipping the rewrite, taking parts costliest first, not writing a
slot's prefix, records at one offset, and a candidate taking its own id instead
of its first's. It also passes under AddressSanitizer and
UndefinedBehaviorSanitizer.

## Reproduction

```sh
./build.sh
./build/compiler test-files
./build/compiler test-files --flat src
clang -dM -E -march=native -fwrapv -funsigned-char -x c /dev/null > build/predefines.h
./build/compiler test-files -march=native -Isrc -fwrapv -funsigned-char --predefines build/predefines.h \
    src/buster/compiler/main.c src/buster/compiler/*_benchmark.c
./build/compiler benchmark-files --flat ~/azp-bench/c-tokenizer/corpus/libc
./build/compiler benchmark-files path/to/sqlite3.c
# in musl-1.2.5, with its generated headers (docs/c-preprocessor.md):
../build/compiler benchmark-files -std=c99 -nostdinc -ffreestanding -D_XOPEN_SOURCE=700 -I./arch/x86_64 \
    -I./arch/generic -Iobj/src/internal -I./src/include -I./src/internal -Iobj/include -I./include \
    $(ls src/*/*.c src/*/x86_64/*.c)
```

On the 9700X, run each under `EXCLUSIVE_OWNER=<name>
~/buster-fast-bench/exclusive.sh`, without `taskset`: the benchmark pins its
lanes. `--bytes map|read|populate` chooses how lanes get files' bytes, and
`--lane-bytes N` the bytes per working lane.

## Not yet

- **Memory reuse.** Clearing fresh pages is the largest single cost on many
  lanes. A process that compiles many translation units (#152) keeps its arenas
  and tables between them, and pays for most of that memory once.
- **One big file on several lanes.** `sqlite3.c`'s 3.8 ms are one lane's.
  Refinement could take a file's token ranges on several lanes (each range's
  first gap needs the token before it, which `refine_tokens` doesn't take yet),
  and the merge already orders occurrences by file and lane-local id.
- **Searching earlier.** A file's literal includes need its kinds, starts and
  bytes, not its refined values: its searches could start while it is refined.
- **`__has_include`.** A unit still probes the paths its `__has_include`
  operands name (`sqlite3.c`'s unit opens one path after the walk).
- **Speculation.** 85% of `sqlite3.c`'s header tokens are never entered. The
  walk could skip includes under conditions that only the predefined macros
  decide, but only where nothing the unit defines first could change them.
- **The other ways of interning** need `c_refine.c`'s pass B to emit
  occurrences or to insert concurrently ("Interning from several lanes").
- **Translation units on lanes** (#152): after the walk, a unit still prepares
  the files the walk couldn't reach (computed includes, its predefines) and
  interns its pastes into the shared table, one unit at a time.
