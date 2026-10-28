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
output stream (kinds, flags, values and locations), `-E` text and diagnostics
when it preprocesses with the cache. Each unit's spellings and diagnostics are
also those it gives with a cache of its own, as the preprocessor runs alone. The
9700X job checks this for the test's tree and generated spellings, this
repository's 53 units and `src` (without its includes), and the preprocessor
oracle's frozen corpora (#262): musl 1.2.5's 1,355 units, `sqlite3.c`, and,
prepared without their includes, the C library corpus (8,983 files). Lua 5.4.8's
34 units passed too before the units' `-E` text and diagnostics were compared.

**The same output whatever the ids.** Ids are opaque (#153): nothing a unit
prints, or a record holds, may depend on the order the shared table hands them
out in. `test-files` runs every check again with the table primed before the
walk, so that it gives its names other ids, on every lane count: every unit's
`-E` text, diagnostics and locations, and every record, spelled the same. On
the 9700X this holds for the same inputs, where every name but at most one in
each run changes ids (597,427 of the C library corpus's 597,428, 41,698 of
`sqlite3.c`'s 41,699, all 15,327 of musl's and all 76,005 of this
repository's).

**Speed**, measured on 2026-10-25 on the 9700X (clang 23.1.1, `build.sh`'s
`-O3 -march=native`) by the 9700X job's `benchmark-files`, under the host's
lock, lane i on CPU i: medians of 21 runs, each from nothing (a new intern table
and cache, and every file opened, read or mapped, lexed, refined and indexed).
The corpora are the preprocessor oracle's frozen ones
([docs/preprocessor-oracle.md](preprocessor-oracle.md)), and the repository's
units those of #262's pull request:

| Lanes | C library corpus, flat | `sqlite3.c` and its headers | musl, 1,355 units | This repository, 53 units |
| ---: | ---: | ---: | ---: | ---: |
| 1 | 74.35 ms | 7.09 ms | 4.93 ms | 14.89 ms |
| 2 | 57.45 ms (1.29×) | **6.98 ms (1.02×)** | 3.86 ms (1.28×) | 12.53 ms (1.19×) |
| 3 | 46.19 ms (1.61×) | 7.04 ms | 3.44 ms (1.43×) | 11.10 ms (1.34×) |
| 4 | 41.61 ms (1.79×) | 7.03 ms | **3.28 ms (1.50×)** | 10.21 ms (1.46×) |
| 5 | 39.14 ms (1.90×) | 7.09 ms | 3.33 ms | 10.25 ms |
| 6 | **38.65 ms (1.92×)** | 7.13 ms | 3.36 ms | 10.52 ms |
| 7 | 39.14 ms (1.90×) | 7.23 ms | 3.48 ms | 9.09 ms (1.64×) |
| 8 | 39.55 ms (1.88×) | 7.31 ms (0.97×) | 3.57 ms (1.38×) | **9.03 ms (1.65×)** |

Main's code before #262, which the same job ran earlier that day in turns
with its variants, took 74.50 ms on one lane and 38.78 on 6 for the corpus,
7.02 and 7.40 on 8 for `sqlite3.c`, 4.83 and 3.25 on 4 for musl, and for the
repository's walk, of 834 files then, 14.94, 12.52 on 3 and 13.70 on 8. On one
lane both take the same path, and their times differ between the two runs by
up to 0.15 ms.

What a run does (the reads and mappings are those of 8 lanes):

| Roots | Rounds | Files | Bytes | Tokens | Shared ids | Opens | Reads, mappings |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| The C library corpus (Zig 0.14.1 `lib/libc`), without includes | 1 | 8,983 | 93.8 MB | 13,152,141 | 597,639 | 8,983 | 8,188, 765 |
| `sqlite3.c` 3.53 | 8 | 420 | 15.1 MB | 1,663,924 | 40,088 | 1,085 | 354, 66 |
| musl 1.2.5's sources, with its build's options | 4 | 1,596 | 2.4 MB | 578,817 | 15,025 | 2,524 | 1,569, 16 |
| This repository's translation units, with `-march=native` | 7 | 839 | 27.4 MB | 3,875,979 | 58,772 | 2,058 | 469, 370 |

- **The C library corpus** is the one large round: 94 MB prepared by every
  lane, 1.92 times as fast on 6 lanes as on one, 340 million tokens a second.
  It scales no further because eight lanes are bound by memory: on 2026-10-22
  their IPC fell from 2.15 on one lane to 0.79, while the clock fell only from
  5.54 to 4.97 GHz (`perf stat` of whole runs). "Where the time goes" has the
  phases.
- **The walks** take a lane per megabyte for their rounds of 4 MB or more, and
  leave the others to lane 0 while every lane probes paths. The repository's
  walk is 1.65 times as fast on 8 lanes as on one, where main's code was 1.09
  times as fast ("The walks, round by round"). musl's rounds are all below
  4 MB, and its walk gains from its 2,524 probes, which take 0.88 ms of a lane
  on 4 lanes against 2.66 on one. Round 0 of `sqlite3.c`'s walk is `sqlite3.c`
  itself, 9.5 MB that lane 0 prepares in 3.4 ms, and the others are below 4 MB:
  the walk takes 6.98 to 7.13 ms on 2 to 6 lanes against 7.09 on one, and up to
  3% longer on 7 and 8 ("Not yet").

**Speculation.** The walk follows every literal include, also those under an
`#if` that turns out to be false. What it prepared that no unit entered:

| Roots | Files prepared | Unused | Their tokens |
| --- | ---: | ---: | ---: |
| `sqlite3.c` | 420 | 250 | 423,621 of 1,663,924 (25.5%) |
| musl's sources | 1,596 | 0 | 0 |
| This repository's units | 839 | 58 | 78,624 of 3,875,979 (2.0%) |

`sqlite3.c`'s unused tokens are 85% of its headers': clang's AVX-512 intrinsic
headers (`avx512fintrin.h` alone is 64,533 tokens), which an
`#include <immintrin.h>` under an `#if` that is false here reaches, and ICU's
`urename.h` (`SQLITE_ENABLE_ICU`). The repository's were mostly the fragment
experiment's sources, which `main.c` included only for that experiment's unit
(not among the benchmark's units, and since removed: [the
decision](c-parser-fragments.md)), and clang's headers for other targets
(`arm_acle.h`, `intrin.h`). musl's units enter every header the walk reaches.

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
- **Directory snapshots for negative include candidates (#653):** on the
  first include in a search directory, its entry-name hashes are retained in
  an optional arena of at most 16 MiB. An absent first path component needs
  no file open. A listed component always takes the existing probe, including
  symlinks and subdirectory names; a hash collision can only retain a probe.
  Dot, dot-dot, empty and overlong components retain the probe too. Complete,
  readable and searchable listings on btrfs, ext4 and tmpfs qualify; the
  directory flags must report that casefold is disabled. Unsupported filesystems,
  permission failures, partial reads and allocation exhaustion fall back to
  the existing search. Include order, `#include_next`, and each file's path
  and system-header level are still decided by `pp_include_candidate`.
  The preprocessor fills listings lazily, so a unit with no includes makes
  none. A run-ahead fills its shared search directories before lanes start;
  the probes on lanes only read the snapshots. The scalar and vector paths
  share this syscall filter; the entry-name hash table is scalar, since its
  purpose is to eliminate OS calls rather than token-processing work.
  As with cached files and negative paths, the filesystem is a snapshot for
  the cache's lifetime: new directory entries require a fresh `FileCache`.
  Listing opens/entries are recorded in `FileCache.directory_opens` and
  `directory_entries`, separately from `FileCacheCounts`' file probes;
  `directory_skips` counts candidates omitted by the listing.
- **A translation unit keeps its own state about each file:** `PPFile` (how a
  path was found, its system level) and `PPFileData`, which copies the record's
  prepared fields when the unit first enters the file and keeps `once` (`#pragma
  once`), the line cursor and whether the unit has entered the file. So guards
  and `#pragma once` work per unit as before, and a unit without a cache of
  its own (`PPOptions.files` null) makes one, so every earlier command runs as
  before: `-E`'s text is byte-identical, and `test-preprocessor --clang`
  passes.
- **File probes in one place:** `file_cache_probe` opens a path, reads its
  identity, and unless a record has that identity, maps or reads its bytes.
  `FileCacheCounts` counts its opens, mappings and reads for whoever called
  it: a unit's `Preprocessor.probes`, or a run's lanes.

### Per-file jobs on lanes

`benchmark-files` and `test-files` start one command-owned `LaneTeam` before
their walks and stop it once at command cleanup. Its capacity is the largest
requested lane count (also covering the built-in tests' two- and three-lane
cases). Successive walks use an active prefix of those same workers; changing
the lane count does not create a new group. Worker scratch is released after
each walk, so this reuses threads without retaining the walk's temporary
memory. The ordinary preprocess/parse command remains single-lane; integrating
the frontend is separate work under #149.

The results above use the team; most of "Where the time goes" but its round
by round section predates it. The timed region starts inside the lane entry
after the warm-up sync, so both the historical and current `benchmark-files`
times exclude thread startup and shutdown. The benchmark prints its current
command-owned lifecycle alongside the timings.

A round of files is prepared by `files_prepare`, which every lane of the group
calls. The files are in the walk's order (the order one lane prepares them
in), and rule 1's costs are their bytes. A round of less than 4 MB
(`BUSTER_FILES_SHARED_BYTES`) is lane 0's alone, and a larger one takes a lane
per megabyte of it (`BUSTER_FILES_LANE_BYTES`), up to all of them
(`files_working_lanes`):

- **One lane** prepares the files one after another straight into the shared
  intern table (`pp_prepare_into`, as a translation unit would).
- **Several** each prepare their files into an intern table of their own, and
  then merge (below). They take rule 1's parts from a counter: `lane_take`'s
  with all the group's lanes, and the run's own (`FilesRun.taken`) with fewer,
  which the other lanes leave alone. With few bytes per lane
  (`BUSTER_FILES_SPLIT_BYTES`), each working lane takes one range instead
  (`range_part_by_cost`).

A lane that joins a round pays for the merge and for the fresh memory of its
arenas and its own table: about 0.5 to 1 ms on the 9700X. `sqlite3.c`'s round
of 3.1 MB took 1.60 ms on 2 of 8 lanes against 1.37 on one, which is what the
4 MB keeps on lane 0; past them, a lane per megabyte rather than per 2 MB made
the repository's walk 1.2 ms faster on 7 and 8 lanes. "Where the time goes"
has the measurements.

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
   (read-only while lanes read it), 16 at a time: it hashes them and prefetches
   their buckets before looking any of them up, so that their misses overlap.
   Those there get their ids; the others are candidates, counted by partition
   of their hashes.
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
   too, a range of ids per lane, 16 at a time with their new buckets prefetched
   for writing: `intern_bulk_replace`), then takes every other candidate's id
   from its spelling's first.
6. **Rewrite.** Each lane rewrites its files' identifier values, and their
   guards, from its own ids to the shared table's (`files_remap`, 64 tokens at a
   time: one compare finds the identifiers, and a gather takes 16 of their
   shared ids at once; for the scalar kernels, `files_remap_scalar`, a token at
   a time, is its twin).

A lane writes only its own spellings and its own segments of the partitions,
and reads the others' through the candidates (rule 2). The shared table's
slots are the one place lanes write at once: a lane takes an empty slot by
exchanging its id, 0, for the spelling's (`intern_place_shared`, a `lock
cmpxchg`), along the same probe sequence as `intern_place`, so the slots a
probe passes stay full and lookups find what they did.

The other two ways, against this one:

- **The exchange of occurrences** (way 2 alone) would move every identifier
  occurrence through the partitions instead of each lane's distinct spellings:
  6.1 million records for the C library corpus against about 750,000
  candidates on 8 lanes, and its interning would still need the first
  occurrences' order.
- **A concurrent table** (way 3) gives ids in the order lanes happen to insert
  them, which changes from run to run. Making them independent of the lanes
  needs each spelling's first occurrence and a rewrite of every identifier:
  this merge's steps 3 to 6 again, after interning against one table that
  every lane writes, whose 16 MB of buckets for the C library corpus no lane's
  cache holds.

Both would need `c_refine.c`'s pass B to change (to emit occurrences, or to
insert concurrently), where the design here uses `refine_tokens` as it is. The
merge's measured cost bounds what either could save: on the C library corpus on
8 lanes, its steps take 13.4 ms of the busiest lanes' time, against 21.0 ms for
preparing ("Where the time goes").

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
- **Bytes.** A lane reads each file of up to 16 KB that it finds into its arena
  (`BUSTER_FILES_READ_LIMIT`), and maps the larger ones. Every mapping holds
  the process's memory map lock, on which lanes that map at once queue, so one
  lane maps with `MAP_POPULATE` and several map without it, which holds the
  lock for less time. Then the lane that prepares a mapped file maps its pages
  in one call (`MADV_POPULATE_READ`) before lexing them, rather than fault them
  in as the lexer reads them.
- **Contexts.** What a file's includes resolve to depends on the file, the
  search directory it was found in (for `#include_next`), whether it is a root,
  and the directory its path is in. A path that leads to a file the walk has
  already taken in the same context is an alias, whose includes the walk
  doesn't read again. The directories compare as directories, by device and
  inode, since `./`, `sub/..` and symbolic links spell one directory many ways:
  a guarded header that includes itself through `./` and `sub/..` would
  otherwise make paths of its own without end. Spellings compare cheaply and
  identities exactly, so a directory's identity is read (one `stat`) only when
  a file has two items to compare. An alias's own path entries are still
  made, but not those of its includes: a unit that follows the alias looks
  those up itself.
- **Speculation stays invisible.** The walk prepares headers that sit under an
  inactive `#if`, which costs time but changes nothing: it runs no macro,
  takes no branch and reports nothing. A missing header is a path entry with
  no record, which a unit reports only if it reaches the include, and a file
  the walk couldn't prepare is left unprepared, for the unit that enters it to
  prepare and say why it can't. A probe opens a path without blocking
  (`O_NONBLOCK`), so that a FIFO is a path with no file, not a wait for a
  writer: the walk probes paths that no unit opens.
- **Failures.** A run that runs out of memory stops, and every lane takes lane
  0's word on it at the same place (a broadcast), so that no lane waits at a
  sync the others skipped. A round whose merge can't finish leaves its files
  unprepared.

### Memory

Every array a lane or a run writes that can grow is in an arena of its own,
whose first 64 KB are base pages (`lane.c`), so that one used for a few
kilobytes doesn't touch a huge page that the kernel clears whole (the
preprocessor's lesson: `docs/c-preprocessor.md`, "Where the time goes"). The
cache reserves each lane's arenas before the lanes start, since reserving takes
the memory map lock. Before the lanes write a fresh array they share (the
candidates, the shared table's new buckets, spans and records), each touches a
range of its pages (`files_touch`): a page that lanes fault at once is cleared
by one while the others wait. Each lane reserves its own table's memory as it
starts, with room for `BUSTER_FILES_MAX_BUCKETS` buckets, and lays the table
out at its first round of several lanes that it works on, at the size of its
share of that round's bytes (`intern_initialize_in`); a lane that never works
on such a round never touches it.

In a group of more than 4 lanes (`BUSTER_FILES_HUGE_LANES`), each lane's
spellings, shared ids and own table start with 1 MB of base pages, and the
token columns of the lanes past lane 0 with 2 MB (`FileCache.lane_small_bytes`,
which `files_run_initialize` sets before the cache reserves them). There, the
lanes that clear fresh huge pages at once are bound by the memory's bandwidth,
while each lane's share of a walk's rounds fills little more than one; with
fewer lanes, each lane's share is larger and huge pages cost less ("Where the
time goes").

## Where the time goes

The C library corpus without includes, on 1 and 8 lanes (ms, on the lane that
spent the most on each phase; medians of 21 runs, the results' run of
2026-10-25):

| Phase | 1 lane | 8 lanes |
| --- | ---: | ---: |
| Preparing (lexing, refining, directive tables) | 55.09 | 19.99 |
| The merge's lookups | | 1.41 |
| The exchange and first occurrences | | 4.42 |
| Additions to the shared table, and taking ids | | 4.63 |
| Rewriting the files' identifiers | | 2.66 |
| Searches and probes (opening, reading and mapping the files) | 17.74 | 4.34 |
| Work on one lane alone | 1.50 | 1.70 |
| Waiting at syncs | | 4.21 |
| The run | 74.35 | 39.55 |

Before #262, the 9700X's profile of the 8-lane run (`perf record`, all
samples, then 40.37 ms): the kernel clearing fresh pages (`clear_page_erms`)
takes 31.7%, the lexer 13.8% and refinement 16.6%. The clearing is the lanes'
fresh memory: the lexer's token columns (9 bytes a token) 11.1% of the
samples, the candidates 3.1%, each lane's own table 2.3%, and the rest of the
merge's arrays and the shared table's new layout and records; reading files
into the arenas, 0.5%. One lane clears the columns too, but eight lanes
clearing at once compete for the same memory bandwidth.

What the profiles changed, each measured on the 9700X (the C library corpus
unless said otherwise, medians of 7 to 21 runs):

- **The memory map lock.** Mapping files with `MAP_POPULATE` from every lane
  took 16% of the samples in `osq_lock`, under `vm_mmap_pgoff`: each mapping
  holds the process's memory map lock. Without `MAP_POPULATE` each mapping
  holds it for less time, and reading files into the lanes' arenas doesn't take
  it at all, but clears and copies fresh memory (8 lanes, before the changes
  below):

  | How lanes get files' bytes | Searches and probes | The run |
  | --- | ---: | ---: |
  | Mapped with `MAP_POPULATE` | 12.94 ms | 50.16 ms |
  | Read into each lane's arena | 9.20 ms | 46.47 ms |
  | Mapped without it | 7.04 ms | 45.17 ms |

  Mapped without it, the mappings still took 14% of the samples in `osq_lock`.
  Most files are small (75% of the corpus's are 4 KB or less, 92% 16 KB or
  less, with 21% of its bytes), so lanes now read the small ones and map the
  rest (11 runs each):

  | Files read | The run, 1 lane | The run, 8 lanes | Searches and probes, 8 lanes | Mappings |
  | --- | ---: | ---: | ---: | ---: |
  | None | 79.32 ms | 44.19 ms | 7.08 ms | 8,953 |
  | Up to 4 KB | 75.18 ms | 40.16 ms | 3.85 ms | 2,234 |
  | Up to 16 KB (the default) | 73.98 ms | 40.56 ms | 4.00 ms | 765 |
  | Up to 64 KB | 74.78 ms | 41.36 ms | 5.10 ms | 229 |
  | Up to 256 KB | 76.22 ms | 42.89 ms | 6.44 ms | 54 |

  `osq_lock` fell to 0.5% of the samples. With 16 KB, musl's walk is 21% to
  26% faster (4.13 ms on one lane against 5.21, 3.09 on 4 against 4.16),
  `sqlite3.c`'s the same, and the repository's 1% faster on one lane but 2% to
  5% slower on 4 to 8. Mapping the larger files with `MAP_POPULATE` on the
  lanes that probe them made `sqlite3.c`'s walk 2% faster on 4 to 8 lanes and
  the corpus 3% slower on 8 (41.5 ms against 40.2), and changed nothing else:
  they stay mapped without it.
- **Fresh huge pages for small arrays.** The run's and each lane's arrays that
  grow first shared arenas, so a lane wrote a few kilobytes into a fresh 2 MB
  page the kernel cleared whole: on `sqlite3.c`'s walk on 8 lanes, 7.6% of the
  samples cleared the pages of `lane_parts_by_cost`'s order. Each now has an
  arena of its own.
- **The merge's memory.** The candidates shrank from 48 to 32 bytes, and the
  lanes touch each fresh array they share (the candidates, the shared table's
  new buckets, spans and records) a range of pages each before writing it, so
  that the lanes clear its pages at once rather than wait for the one that
  faults a page first: the merge's steps went from 14.5 to 13.2 ms (the sum of
  each step's busiest lane, 8 lanes).
- **Which lanes work.** Every lane on every round made the walks slower than
  one lane: `sqlite3.c`'s took 8.09 to 8.90 ms on 2 to 8 lanes against 7.46 on
  one (`--lane-bytes 0`). A working lane per 1 MB still left it at 8.72 ms on 8
  lanes, against 7.53 on one, and the repository's walk at 6.09 against 5.46.
  At 4 MB per lane (and at 8 MB, which measured the same) they took 7.62 against
  7.47 and 5.32 against 5.37 (both before the files' reads). Once a lane that
  joins a round cost less (below), a lane per megabyte from 4 MB made the
  repository's walk faster still, and left `sqlite3.c`'s rounds to lane 0.

### The walks, round by round

`benchmark-files --rounds` prints where each lane count's time went round by
round (#262). On 2026-10-25 the repository's translation units, 53 with the
benchmarks', walked 834 files of 27.0 MB in 7 rounds, two of them larger than
4 MB. Before #262 the walk took 14.94 ms on one lane and 13.70 ms on 8, though
12.52 ms on 3; now 9.08 ms on 8 (ms, medians of 21 runs on the 9700X; in
parentheses, the lanes that prepared the round):

| Round | Files | Bytes | 1 lane | 8 lanes, before | 8 lanes |
| --- | ---: | ---: | ---: | ---: | ---: |
| The roots' search | | | 0.21 | 0.22 | 0.23 |
| 0 | 53 | 1.56 MB | 1.91 | 1.83 (1) | 1.82 (1) |
| 1 | 265 | 16.66 MB | 7.91 | 6.74 (3) | 3.97 (8) |
| 2 | 347 | 8.06 MB | 4.33 | 4.40 (1) | 2.53 (7) |
| 3 to 6 | 169 | 0.73 MB | 0.55 | 0.47 | 0.48 |
| The walk | 834 | 27.0 MB | 14.94 | 13.70 | 9.08 |

The job's run of the final code, on the 839 files of the units after main's
later changes, measured alike: 14.89 ms on one lane and 9.03 on 8, round 1 on
8 lanes in 3.98 ms and round 2 on 7 in 2.45.

Before, a lane per 4 MB left round 2 to lane 0, and round 1's 3 lanes took
6.74 ms in a group of 8 against 5.94 in a group of 3. At that round every lane
of the group laid out a table of its own, the 5 with no files in it too, and
every table, array and token column started in fresh huge pages: the working
lanes laid out their tables in 0.95 to 1.23 ms, against 0.40 in a group of 3,
and lexed in 2.09 to 2.29 ms against 2.01 to 2.07. The changes, each measured
against the others in the same runs (variants of `files_prepare` that the
benchmark ran in turns, since removed):

- **A lane per megabyte from 4 MB, and parts.** Rounds 1 and 2 now take 8 and
  7 lanes. At 2 MB per lane instead, the walk took 10.23 and 10.28 ms on 7 and
  8 lanes, against 9.06 and 9.05. A round of fewer working lanes than the
  group's has them take parts from the run's own counter, as `lane_take` gives
  them to all of a group's lanes, rather than a range each, so that a lane that
  finishes its part early takes another. Below 4 MB a round stays on lane 0,
  where a merge costs more than it saves:
  `sqlite3.c`'s round of 3.1 MB took 1.60 ms on 2 of 8 lanes against 1.37 on
  one.
- **Base pages in groups of more than 4 lanes** ("Memory"). Round 1 lexed in
  1.1 to 1.4 ms per lane on 8 lanes with them, against 2.1 to 2.6 with huge
  pages, and the walk took 9.04 ms against 11.11 on 8 lanes and 9.03 against
  10.52 on 7. On 2 and 3 lanes, where each lane's share fills more pages, they
  made it slower (12.95 and 11.33 ms against 12.22 and 10.87), so groups of up
  to 4 lanes keep huge pages.
- **The lanes' tables reserved as the lanes start.** A lane lays its table out
  at the first round of several lanes that it works on, and one that never
  works on such a round never touches it. The walk measured the same (9.04 ms
  against 9.06 on 8 lanes).
- **Mapped files populated before they are lexed.** On several lanes the
  probes map files without `MAP_POPULATE`, and the lexer took their pages'
  faults: lane 0 lexed `sqlite3.c` in 2.38 ms on 8 lanes, against 2.05 on one
  lane, which maps it with `MAP_POPULATE`. After `MADV_POPULATE_READ` (0.11 ms)
  it lexes it in 2.22 ms. Populating at the probes instead made `sqlite3.c`'s
  walk 0.1 ms faster and the repository's 0.3 to 0.5 ms slower on 4 to 8
  lanes.
- **The merge's misses** ("Interning from several lanes", steps 1, 5 and 6).
  Before the changes above, the prefetches and the gather made the
  repository's walk 13.30 ms on 2 lanes against 13.79, 12.01 on 3 against
  12.45 and 13.32 on 8 against 13.66. On the C library corpus, the lookups on 2
  lanes took 2.08 ms against 2.29 and the rewrite 3.65 against 4.40, though on 8
  lanes the rewrite took 2.66 against 2.43; the run took 57.75 ms on 2 lanes
  against 58.86, and the same on 6 and 8 lanes.

Two others didn't pay. Starting each working lane on a copy of the shared table,
so that its known names refine to their shared ids and only the round's new
ones need the merge, made the walk slower on 2 to 8 lanes (11.86 ms on 8
lanes against 11.00, before the base pages). Taking a wave's probes from a
counter, rather than a range per lane, made the repository's walk 0.02 to
0.19 ms faster on 2 to 8 lanes and `sqlite3.c`'s 0.01 to 0.14 ms slower.

Now, round 1's 8 lanes lay out their tables in 0.38 ms and lex in 1.12 to
1.44 ms each. Round 2's 7 lanes prepare in 1.00 to 1.11 ms and merge in
1.08 ms, 0.76 of it in the additions: the shared table grows, and the lanes
place its old spellings again ("Not yet").

## Correctness

`./build/compiler test-files [--lanes LIST] [--seed N] [--id-order N] [--keep]
[--flat] [options] [path...]` (about 0.7 s without paths on the 9700X) runs
every check on each lane count of `--lanes` (1, 2, 3 and 8), each time with a
new intern table and cache, and compares the results with the first lane
count's, byte for byte, as "Results" says, and every unit with itself run
alone; and nothing may be written to stderr while the cache is filled.

Then it runs every check on each lane count again with the shared table's ids
in another order, as `test-preprocessor --id-order` does for its cases
(`docs/determinism.md`). Before the walk fills the cache, the table is primed
with 17 to 47 names no file has, then with every other name of the first run's
table, in reverse from a place the seed picks (`--id-order N`, 153 by default,
plus the lane count's place in the list). The walk finds those in the table and
gives the others ids after them, and every lane prepares every round, so that
on more than one lane the merge does: its lookups find the primed spellings,
and its additions number the rest from past them. The predefined names keep
their fixed ids, the table's contract. The first run's other names must all be
in the table, which may hold no others, and both those primed and those added
after them must have changed ids (all but about one name in each run do). Every
unit must give the first run's `-E` text, with and without line markers, and
its diagnostics, byte for byte, and its stream's kinds, flags and locations,
each identifier's and keyword's id spelled as the first run's; every record must
be the first run's, its identifiers' values and its guard spelled the same, and
the walk must do what it did. The checks:

- **The tree:** three translation units and the headers they include in a
  directory of the test's own. It has includes by the includer's directory and
  through `-I` and `-isystem`, an `#include_next` chain through three
  directories, a header reached by four paths (a symbolic link and `..` among
  them), a hard link to a header a computed include also names (which the walk
  can't follow: the unit finds the file prepared under its other name), guarded
  and `#pragma once` headers included twice, a header under `#if 0` that exists
  (prepared but never entered) and one that doesn't (no diagnostic), and a
  missing header in an active line, which the unit reports. Two warnings name
  identifiers, through their ids: an `#if` in a header that two units include,
  whose notes name the macros it was expanded from, and a macro redefined. The
  counts are exact: 13 files prepared, each identity once, in 4 rounds; 1
  unused; 2 requests not found; the units' errors (0, 0, 1) and warnings (1, 2,
  0); and after the walk, the unit with the computed include opens one path and
  the others none. It runs with the vectorized kernels and their scalar twins,
  its rounds cut into parts and split once, all on every lane (its rounds are
  small enough for lane 0 alone otherwise).
- **Spellings:** 240 generated files of identifiers (a shared vocabulary drawn
  unevenly, names of their own, names past 16 and 63 bytes, line splices and
  UCNs), prepared without following includes, and a translation unit that
  includes them all, which reads them from the cache. They make 165,562 ids, so
  the shared table grows during a merge. They run in parts, split once, and with
  two lanes working and the others idle, in parts (from the run's counter) and
  split once, and in two runs into one cache, which must equal one run on one
  lane.
- **Contexts:** a guarded header that includes itself through `./` and
  through `sub/..`, which the walk must take once rather than make paths of
  without end, and one header in two directories (a hard link) whose own
  `local.h` differ, which it must walk in both: 5 files in 3 rounds, and the
  unit opens nothing after the walk.
- **Rounds:** the trace that `benchmark-files --rounds` prints, on two walks
  of a chain of headers, each including the next, a file a round: of exactly
  the 16 rounds the trace keeps apart (`BUSTER_FILES_TRACE_ROUNDS`), and of 21.
  On every lane count it must keep each round's file, bytes and working lane,
  the last kept round with every later one, and snapshots that go forward on
  every traced lane (the TSC, each phase's ticks, the waves and the probes)
  from the walk's start to its end, the one after the last kept round's (#422's
  review: the end overwrote the last round's start).
- **Special paths:** a FIFO that an `#include` under `#if 0` names, which the
  walk mustn't block opening, and that an active one names (an error, as the
  unit gives alone); and a quoted name of 2,400 bytes under `#if 0`, twice in a
  file and once in another, whose directory is 2,539 bytes long: the three
  requests' search key holds both whole, and they share one search.
- **Memory:** runs on 2 and 3 lanes that all work, with the round's arena
  from 64 bytes to 32 KB (half again each time) or the first or last lane's
  scratch memory from 8 bytes to 512 (four times each time): 48 runs, of which
  38 fail, at the walk's arrays, the merge's or a file's preparation. Every
  unit must give what it gives alone.
- **Files:** each path's `.c` and `.h` files as translation units with `-E`'s
  options, all the walk's roots; with `--flat`, only prepared. Their files of
  more than 16 KB are mapped and the others read, as all the tree's are.

The checks that would hang if they regressed (a probe blocking on a FIFO, a walk
without end, lanes waiting at different syncs) run under a 60-second alarm,
which fails the test.

Six mutations of the merge each fail it: taking the last occurrence instead of
the first, skipping the rewrite, taking parts costliest first, not writing a
slot's prefix, records at one offset, and a candidate taking its own id instead
of its first's. So do reverting the fixes of #249's review, one at a time:
the search key's room (the long name's requests take 3 searches), the merge's
steps after an allocation failure (a crash), the aliases (the walk doesn't
end) and the probe's non-blocking open (it blocks); reading `run->failed` on
each lane at the walk's start, the race the memory check found, hangs only
when the lanes' timing goes one way. It also passes under AddressSanitizer and
UndefinedBehaviorSanitizer.

Output that depends on the ids passes every lane count's comparison, since
every lane count gives the same ids; the check with the ids in another order
fails it. Three mutations show it, each built and run with that check and
without it:

- the vectorized printer doubling the space before an identifier whose id is
  odd: only that check fails, on the tree's, the spellings', the contexts' and
  the special paths' `-E` text (the tree's scalar ways print with the scalar
  printer);
- a macro's id after its name in the "expanded from macro" note: that check
  fails on the tree's diagnostics, and without it so does the comparison with
  each unit alone, whose table gives the names other ids;
- the merge numbering a round's new spellings from the predefined names' end,
  as if the shared table held nothing else: on the spellings, a single round,
  only that check fails (a third of the first run's names go missing from the
  table); the tree's lane counts fail too, from its second round on.

## Reproduction

```sh
./build.sh
./build/compiler test-files
./build/compiler test-files --flat src
clang -dM -E -march=native -fwrapv -funsigned-char -x c /dev/null > build/predefines.h
./build/compiler test-files -march=native -Isrc -fwrapv -funsigned-char --predefines build/predefines.h \
    src/buster/compiler/main.c src/buster/compiler/*_benchmark.c
./build/compiler benchmark-files --flat ~/azp-bench/c-tokenizer/corpus/libc
./build/compiler benchmark-files --rounds path/to/sqlite3.c
# in musl-1.2.5, with its generated headers (docs/c-preprocessor.md):
../build/compiler benchmark-files -std=c99 -nostdinc -ffreestanding -D_XOPEN_SOURCE=700 -I./arch/x86_64 \
    -I./arch/generic -Iobj/src/internal -I./src/include -I./src/internal -Iobj/include -I./include \
    $(ls src/*/*.c src/*/x86_64/*.c)
```

On the 9700X, run each under `EXCLUSIVE_OWNER=<name>
~/buster-fast-bench/exclusive.sh`, without `taskset`: the benchmark pins its
lanes. `--read-limit N` sets the largest file the lanes read rather than map
(16 KB), `--bytes map|read|populate` how they get the larger ones,
`--shared-bytes N` the fewest bytes of a round of several lanes (4 MB),
`--lane-bytes N` a round's bytes per working lane (1 MB), and `--rounds` prints
where each lane count's time went round by round. The 9700X job runs the
benchmark on the preprocessor oracle's frozen corpora (musl's 1,355 units,
`sqlite3.c` and the C library corpus) where a pull request changes
`c_files*.c`, and the tests on them in its test list.

## Not yet

- **Memory reuse.** Clearing fresh pages is the largest single cost on many
  lanes. A process that compiles many translation units (#152) keeps its arenas
  and tables between them, and pays for most of that memory once.
- **One big file on several lanes.** `sqlite3.c`'s 3.4 ms (3.7 on 8 lanes) are
  one lane's.
  Refinement could take a file's token ranges on several lanes (each range's
  first gap needs the token before it, which `refine_tokens` doesn't take yet),
  and the merge already orders occurrences by file and lane-local id.
- **Preparing where the bytes are.** The lanes that probe a round's files read
  the small ones, and the lanes that prepare the round lex them from the other
  cores' caches. Reading each file on the lane that prepares it, once the round
  is known, would keep it in that lane's caches.
- **The rounds lane 0 prepares alone, on many lanes.** Round 0 of the
  repository's walk (53 files, 1.56 MB) took 1.91 ms on one lane, 1.39 on 2 and
  3, and 1.82 on 8: lane 0's share of the probes took 0.36 ms on 3 lanes and
  0.61 on 8, though it is smaller, and the other lanes' up to 0.73. What the
  probes wait on there hasn't been measured. `sqlite3.c` itself, round 0 of its
  walk, took 3.62 ms on one lane and 3.78 to 3.86 on 2 to 8: lane 0 prepared
  it in 3.58 to 3.75 ms against 3.41 on one lane.
- **The shared table's growth in a merge.** Of round 2's merge on 7 lanes,
  1.08 ms, 0.76 are the additions: the shared table grows there, and the lanes
  place all its old spellings again, a range each (on one lane, the growth is
  part of lane 0's preparing). No lane uses the shared table while a round of
  several lanes is prepared, and a round knows its bytes before then: a lane
  could grow the table while the others prepare, where the bytes say the round
  will need it.
- **Five and six lanes.** The repository's walk took 10.42 and 10.61 ms on 5
  and 6 lanes, against 10.21 on 4 and 9.14 on 7. On 5 and 6 lanes, each lane's
  share of round 1 (2.8 to 3.3 MB) starts its own table at 16,384 buckets, whose
  1 MB fills the table's base pages, with its spans and records past them in a
  fresh huge page: lane 0 laid it out in 0.72 and 0.80 ms, against 0.27 on 4
  lanes, in huge pages, and 0.35 on 7, whose tables of 8,192 buckets fit the
  base pages. The base pages could follow the table's size.
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

## Boundary audit #298

Audit pin: `42630114aeb0553c3e2fabfd7fe9bc84b0ce6126`;
[scope and evidence](simd-boundary-audit.md). The
[file-job and merge map](https://github.com/buster14a/buster/blob/42630114aeb0553c3e2fabfd7fe9bc84b0ce6126/src/buster/compiler/c_files.c#L4)
and [cached record](https://github.com/buster14a/buster/blob/42630114aeb0553c3e2fabfd7fe9bc84b0ce6126/src/buster/compiler/c_preprocessor.c#L390)
cover more than the preparation kernel:

| Producer → consumer | Storage, batch and ordering |
| --- | --- |
| `file_cache_probe` → `pp_prepare_into` | Owned mapped/read bytes and per-file columns; checked size and mode configuration. Cache identity is device/inode, with separate path/search context. |
| `pp_prepare_into` → `files_read_requests` and unit `pp_copy_prepared` | `PPFileData` token/directive/guard columns are shared; inclusion-specific once/line/location state belongs to the unit. Cache storage outlives borrowing units. |
| Lane-local intern tables → `files_find_new`/exchange/`intern_bulk_begin`/`files_take_ids` → `files_remap` | Candidates retain 16 prefix bytes, a u64 hash and packed first-occurrence key. Count/place/fill and barriers assign disjoint ranges. IDs follow file then first-token order, not completion order. |
| Directive include spans → `files_run_ahead` → later PP lookup | Frontier rounds over literal includes, search candidates in legal directory order. Computed includes remain with ordered PP. Speculative reads emit no diagnostic or macro effect. |

Shared output is published only after every participating lane agrees a phase
succeeded. One lane reserves table ranges; each lane fills its own segment;
readers do not race a shared intern-table growth. Candidate bit fields are
bounded by compile-time file/lane/ID limits, with runtime arena/count failures.
Per-file token capacities and tails are the lexer/refinement contracts, not
inferred from mapping size. Intern remap touches identifier values only.

A round chooses working lanes from bytes (currently one per MiB from 4 MiB,
limited by the group); smaller rounds use lane 0 and avoid merge entirely. Counter parts
keep each lane's files in order, not costliest-first, because local IDs must
preserve first occurrence. Internal 16/32/64-item vector work does not change
round completion or search dependencies. Empty rounds terminate; inactive
lanes still participate in the required synchronization decisions.

Observed extra work includes speculative files never entered, per-lane new-name
exchange and token-ID remapping. Those costs buy ready shared files and
deterministic identities. Compare shared-ID/direct single-lane preparation,
working-lane thresholds and bounded run-ahead using total unit preprocessing,
cache preparation/release, allocations and unused bytes. The rejected [#290](https://github.com/buster14a/buster/issues/290)
read experiment is not a retained optimization. [#262](https://github.com/buster14a/buster/issues/262) retains walk/scaling
acceptance; [#111](https://github.com/buster14a/buster/issues/111) owns inclusion replay; [#152](https://github.com/buster14a/buster/issues/152) owns a real multi-unit driver.

Disposition: retain the measured cache/merge design and its exact single-lane
reference. `test-files`, PP comparisons and determinism tests cover aliases,
read modes, guard skips, delayed lanes, failure decisions and identical output.
The results above measure preparation/walk workloads; they do not establish a
whole multi-unit compiler speedup or justify moving ordered macro state into
speculative jobs.
