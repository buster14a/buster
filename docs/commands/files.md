# files

Run commands and resolve source paths from the repository root. Follow the
[shared agent rules](../../AGENTS.md), including the 9700X lock and `cores`
requirements. [Command index](README.md).

## Module overview

- `src/buster/compiler/c_files.c` is #150's per-file jobs: files prepared on a
  group of lanes into a `FileCache`, each lane interning into a table of its
  own that a merge (rule 4's exchange) gives the shared table's ids in the
  order of first occurrences, so that they don't depend on the lane count; and
  the run-ahead, a frontier walk over the files' literal includes by the
  preprocessor's own search. `docs/c-files.md` has the design and the 9700X
  measurements. `c_files_test.c` holds its tests (`main.c` includes it;
  `./build/compiler test-files` runs them) and `c_files_benchmark.c` provides
  `benchmark-files`.
## Per-file jobs tests

`./build/compiler test-files [--lanes LIST] [--seed N] [--id-order N] [--keep]
[--flat] [options] [path...]` (about 0.7 s without paths) checks `c_files.c`.
Every check fills a file cache on each lane count of `--lanes` (1, 2, 3 and 8),
each time with a new intern table and cache, and must give the first lane
count's results byte for byte: the shared table's ids and spellings, every
record's tokens, values, leading-space bits, directive table and guard, what the
walk did, and each translation unit's output stream (kinds, flags, values and
locations), the text `-E` prints for it with and without line markers and its
diagnostics when it preprocesses with the cache. One lane refines straight into
the shared table, so it is the merge's scalar twin. Each unit's spellings and
diagnostics must also be those it gives with a cache of its own, with as many
errors and warnings, and nothing may be written to stderr while the cache is
filled.

Then every check runs again on each lane count with the shared table's ids
handed out in another order (#153: ids are opaque, and nothing may depend on
their order), as `test-preprocessor --id-order` does for its cases. Before the
walk fills the cache, the table is primed with 17 to 47 names no file has, then
with every other name of the first run's table, in reverse from a place the
seed picks (`--id-order N`, 153 by default, plus the lane count's place in the
list), so that the walk finds those in the table and gives the others ids after
them; every lane prepares every round, so that on more than one lane the merge
does. The predefined names keep their fixed ids. The first run's other names
must all be in the table, which may hold no others, and both those primed and
those added after them must have changed ids. Every unit must give the first
run's `-E` text (both modes) and diagnostics byte for byte, and its stream's
kinds, flags and locations, each identifier's and keyword's id spelled as the
first run's; every record must be the first run's, each identifier's value and
the guard spelled the same, and the walk must do what it did:

- the tree: a directory of the test's own (under `$TMPDIR` or `/tmp`) with
  three translation units and the headers they include, by every kind of
  include the walk resolves: the includer's directory, `-I` and `-isystem`, an
  `#include_next` chain through three directories, a header reached by four
  paths (a symbolic link and `..` among them), a hard link to a header a
  computed include names, guarded and `#pragma once` headers included twice, a
  header under `#if 0` that exists and one that doesn't, and a missing header
  in an active line; and two warnings that name identifiers: an `#if` in a
  header two units include, whose notes name the macros it was expanded from,
  and a macro redefined. The counts are exact: 13 files prepared (each identity
  once) in 4 rounds, 1 unused, 2 requests not found (no diagnostic), the units'
  errors and warnings, and their system calls after the walk (only the
  computed include's). It runs with the vectorized kernels and with their
  scalar twins, cut into parts and split once, on every lane
  (`FilesRun.lane_bytes` 0);
- spellings: 240 generated files of identifiers (a shared vocabulary drawn
  unevenly, names of their own, long names, line splices and UCNs) prepared
  without following includes, 165,562 ids, so that the shared table grows in a
  merge, and a translation unit that includes them all: in parts, split once,
  and with two lanes working (in parts from the run's own counter, and split
  once), and in two runs into one cache, which must equal one run on one lane;
- contexts: a guarded header that includes itself through `./` and `sub/..`,
  which the walk must take once (an alias of a file in the same context, by
  its directory's device and inode, isn't walked again), and one header in two
  directories (a hard link) whose own `local.h` differ, walked in both;
- rounds: `benchmark-files --rounds`' trace on walks of a chain of headers,
  a file a round, of exactly the 16 rounds the trace keeps apart and of 21: it
  must keep each round, the last kept one with every later one, and snapshots
  that go forward on every traced lane from the walk's start to its end;
- dollars: a cache filled with `$` as punctuation (`-fno-dollars-in-identifiers`,
  #281), whose unit must split `aa$bb` into three tokens on every lane count;
- special paths: a FIFO that an `#include` under `#if 0` names (the walk's
  probe mustn't block) and an active one names (an error, as alone), and a
  quoted name of 2,400 bytes in a directory of 2,539, three requests of which
  must share one search;
- memory: runs on 2 and 3 lanes short of the round's arena or of one lane's
  scratch memory, at sizes that fail the walk's, the merge's and a file's
  allocations: every unit must give what it gives alone;
- with paths, their `.c` and `.h` files as translation units with `-E`'s
  options (`-I`, `-isystem`, `-D`, `--predefines`, `-std=`, ...), all the walk's
  roots; with `--flat`, only prepared (a corpus such as the C library's).

The checks that would hang if they regressed run under a 60-second alarm, which
fails the test.

`--seed N` changes the spellings, `--id-order N` the ids' order, and `--keep`
leaves the directory. On the 9700X, `sqlite3.c`, musl 1.2.5's 1,357 sources and
the C library corpus (`--flat`) pass too, and Lua 5.4.8 did before the ids'
order and the units' `-E` text and diagnostics were checked.
The 9700X job's test step runs it, on `src` with `--flat`, on the
repository's translation units and on the preprocessor oracle's frozen corpora
(#262): musl's 1,355 units with its build's options, `sqlite3.c`, and the C
library corpus with `--flat`.

## Per-file jobs benchmark

`./build/compiler benchmark-files [--lanes LIST] [--cpus LIST] [--runs N]
[--spin MS] [--flat] [--rounds] [--bytes map|read|populate] [--read-limit N]
[--shared-bytes N] [--lane-bytes N] [options] path...` fills a cache from the
paths (with `--flat`, without following their includes) on each lane count of
`--lanes` (1 to 8), lane i pinned to the i-th CPU of `--cpus` (the allowed
CPUs, in order), every run from nothing after `--spin` ms (2) of spinning on
every lane. `--read-limit` sets the largest file the lanes read rather than map
(16 KB), `--bytes` how lanes map the larger ones (or read them),
`--shared-bytes` the fewest bytes of a round that several lanes prepare (4 MB),
and `--lane-bytes` a round's bytes per working lane past them (1 MB). It
reports medians of `--runs` (21):
the run's time, speedup and time to the roots' round, the tokens per second,
each phase on the lane that spent the most on it (preparing, the merge's
lookups, exchange, additions and rewrite, the include lines, the searches, the
work one lane does alone, and the waits), how busy the lanes were, what a run
did, and without `--flat` the files the walk prepared that no unit entered,
with the largest. With `--rounds`, it then prints where each lane count's time
went round by round: each round's files, bytes, working lanes, waves, probes
and phases on lane 0 and on the other lanes that spent the most and the least
on them, from a trace the walk keeps only when asked (`FilesRun.trace`). Run it
under the 9700X's lock without `taskset`: it pins its lanes. `docs/c-files.md`
has the numbers for the C library corpus, `sqlite3.c`, musl and this
repository. The default 9700X job runs it on the repository's translation
units, with `--rounds`, and where a pull request changes `c_files*.c` (the
`changes` job's `files`), on the frozen corpora too: musl's units, `sqlite3.c`
with `--rounds` and the C library corpus with `--flat`. Like the others, its
measurements are informational.
