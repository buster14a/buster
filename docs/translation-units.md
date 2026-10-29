# Many translation units in one process

[#152](https://github.com/buster14a/buster/issues/152): the C driver compiles
every source file of its command line in one process.

```sh
./build/compiler [options] a.c b.c ... -o prog    # one executable of them all
./build/compiler -c [options] a.c b.c ...         # a.o, b.o, ...
./build/compiler -fsyntax-only [options] a.c b.c ...
```

With one source file the driver runs as before, byte for byte. With several:

- **`-fsyntax-only`** checks each unit.
- **`-c`** writes each unit's object, named as clang names it: the source's
  base name with `.o`, in the current directory. `-o` with `-c` is refused, as
  clang refuses it ("cannot specify -o when generating multiple output
  files"), and so are two units whose objects would have one name, which clang
  would write twice.
- **The default command** links one executable of them all (`-o`, `a.out` by
  default) through the direct path (#157): the units' links, merged into one
  and bound against the C runtime's snapshot as one unit's link is
  (`check_driver_executable`). `-l` and `-L` work as for one unit.
- **`-g`** gives the executable each unit's DWARF ("The DWARF" below), and
  `-c -g` each unit's object its own.
- `--dump-typed`, `--dump-static` and `--id-order` take one source file.

Every unit is compiled whatever the others do, as clang compiles them: a unit
with errors writes no object, and the others write theirs; any unit's error
makes the exit status 1 and leaves no executable. The diagnostics come in the
command line's order, each unit's exactly as it alone would print them.

`src/buster/compiler/c_checker_units.c` is the implementation, and
`c_checker_driver.c`'s `check_driver_unit` the pipeline each unit runs, the one
a command of one file runs.

## The units on lanes

The units are a lane group's items (`lane.c`), taken costliest first from one
counter (`lane_take`), their costs their source files' sizes: the largest
first, so that the last ones to finish are small, and the command line's
order among equals. Each unit runs on one lane from preprocessing to its code:
the parse, the checker, the selection, the lowering, Tier 0, branch sizing,
encoding and the object's write or the link's copy. A unit's functions compile
on its lane, and on the lanes that have no unit left to take ("The functions
on the units' lanes" below): #151's function lanes inside a unit's lane would
be a lane group inside another, which `lane_run` refuses. With many units the
units fill the lanes: on the 9700X's eight cores, more than a few units keep
every lane busy.

The driver runs a lane per unit, and past the units one per 32 KB of their
sources (`BUSTER_CHECK_UNITS_LANE_BYTES`), whose only work is the units'
functions' parts (#777, below); by default no more than one per CPU the
process may run on, and with `--lanes N` no more than N; and the worker budget (#673:
`--worker-budget`, GNU make's jobserver) limits the lanes past the calling
thread's own as it limits #151's. Its lease covers the units' lanes and the
executable's writers, which run on as many, and its slots go back once both
are done. Lanes that can't start leave their units to the calling thread,
which compiles them alone: the same outputs.

The command's lanes are one team (`lane_team_start`, as `link-foreign`'s:
#501), started before the units run: the units' run, the merge of their links,
its `link_check` and the executable's write are each an operation of the
team's workers, which keep their threads and scratch arenas from one to the
next rather than starting and joining threads of their own (#778).

Before the lanes start, the calling thread builds the tables every unit's code
reads and the first unit to need them would otherwise build: the parser's
(through `parse_initialize`, in the front end's home: `main.c`), the
encoder's, the semantics' and Tier 0's, and with unwind tables, theirs. The
lanes then only read them.

### The functions on the units' lanes

A unit's functions can't run lanes of their own, so a command of a few large
units ran a lane per unit, and its lanes without a unit waited: `sqlite3.c
shell.c` took longer than `sqlite3.c` alone on one lane (#777). The units'
lanes are a board instead (`CheckEmitBoard`, `c_check_program_emit.c`):

- **A unit's emission that starts while one of the lanes takes no units** (the
  board counts the lanes that still take them) cuts its bodies into parts as
  lanes of its own would, by #151's rule (`parts_by_cost`), opens them as a new
  epoch of its lane's share, and takes them itself, costliest first.
- **A lane with no unit left to take** takes the parts of any lane's share
  (`check_program_emit_help`), and while none has a part it waits on the
  board's word, which a share's opening and a lane's last unit advance
  (`lane_advance`), until no lane takes units. It sleeps on the word's futex at
  once (`lane_sleep`): it waits for another lane's front end, and a core that
  spun or waited in `mwaitx` meanwhile would draw power that the cores at work
  lack for their clocks.
- **Each lane that takes parts of an emission** has a writer and arenas of its
  own in it, as a function lane does (its place on the board plus one; the
  sharing lane's are the caller's), which the sharing lane releases after its
  unit. The parts are #151's steps, which the sharing lane merges in source
  order once the last one is done: every object and executable is one lane's.

A take is a compare-and-swap of the share's ticket, the epoch's next part beside
the epoch, whose epoch must be the share's limit's: a lane that read one epoch's
ticket takes no part of the next. The last part done publishes the epoch
complete, which the sharing lane waits for before its emission goes on, so no
lane reads an epoch's run after its emission. An emission that starts while
every lane still takes units runs alone, its bodies one part, as before: a
command of many units pays for the board only once its units run out, when its
last units' emissions share their parts.

On the 9700X's CPUs 0 to 7, SQLite 3.53.4's `sqlite3.c` (9.5 MB) and `shell.c`
(1.2 MB) as one command, against the same executable's two processes at once,
whole processes' medians (`time-runs`, 21 runs, each row twice); [the
record](https://github.com/buster14a/buster-performance/tree/afdfdd4051b834e5a880766a87bf7f464be3b437/docs/measurements/units-functions-777-cache-776-2026-10-29) has the commands and logs:

The two-process column is an **unverified historical control**. Its shell
commands use operand-free `wait`; the recorded zero failures count wrappers,
not each compiler child, and no retained per-child status or fresh-output receipt
proves every timed compile succeeded. The linked control can reuse old objects.
This establishes no actual compiler failure, but the derived 0.9% compile gap
and 8.6% linked speedup against that column cannot qualify performance
acceptance. The synchronous one-command parent/head rows and tests have their
own recorded scope. The record preserves the original scripts, numbers and logs;
this correction adds no measurement, and #777 remains open.

| ms | #778 | #777 | two processes at once (unverified) |
| --- | ---: | ---: | ---: |
| `-c` | 133.86 | 122.04, 122.18 | 121.08, 120.85 |
| linked | 138.25 | 125.97 | 137.77, with `cc`'s link of their objects |

- The large unit's emission shared its parts with the seven other lanes, which
  took 1,020 of them. `sqlite3.c` alone with `-c` takes 114.10 ms. The recorded
  0.9% gap against the two-process wrappers is unverified; it does not establish
  a slowdown or its cause in one process's address space.
- The command's peak resident set is 537 MB, against #778's 436: the other
  lanes' writers and arenas for the parts they take. The historical larger
  process's 424 MB is from the unverified two-process control.
- Its lanes without units sleep on the board's futex at once: waiting with
  `lane_wait`'s spin and its millisecond in `mwaitx` took 192 ms of user time
  against 164 to 171, with no difference in wall time.
- Lua's, synth's and musl's commands by 8 lanes, which have no lanes without
  units until their last units, run as #778's (34.47 ms against 34.65, 530.34
  against 528.40, 238.36 against 240.57), with the same peaks.

## What the units share

Each lane keeps an intern table and a file cache (#150's `FileCache`) that the
units it compiles preprocess with, one after another: a header that the lane's
units include is read, lexed, refined and given its directive table once on
the lane, and a path once probed. The lanes share nothing that a unit writes,
so no lane waits for another, and no structure is shared between threads but
the tables built before the lanes.

A unit's own state is its own, as #152's design asks: its macro table, its
bindings and its type table (tags and their types, and the builtin types each
table makes for itself). Its identifiers that the lane's earlier units gave
ids keep them, and its other identifiers get the next ones: the ids a unit
sees depend on which units its lane compiled before it. Ids are opaque
([docs/determinism.md](determinism.md)): nothing a unit writes depends on
them, so a unit's object is the same bytes whichever lane compiles it and
after whichever units. `test-check-units` checks it: each unit's object of a
command of several units is its object alone, at every lane count.

### One cache for all the lanes, measured

#152's design shares one file cache and one intern table between all the
units, so that a header is prepared once in the process, as #150's run-ahead
walk prepares a command's files on lanes. A unit's preprocessing writes both:
the spellings that token pasting makes, the paths its `__has_include` and
computed includes probe, and a file the walk didn't prepare (pp_run's
`intern_find_or_add`, `file_cache_lookup_path` and `file_cache_prepare`). Each
lane's cache prepares a header at most once per lane instead: on 8 lanes, a
header that every unit includes is prepared 8 times rather than once.

#776 measured the sharing that writes nothing shared while the units run:
#150's walk (`files_run_ahead`) from every unit's main file, on the command's
lanes before the units, into one cache and table, of which each lane's were
then copies (the walk's records and paths copied by value, their files and
prepared columns borrowed; its spellings under their ids), so that a lane
wrote only its own, without a lock. It was slower on every command, and kept
more memory, so the compiler doesn't have it: the experiment (`--units-walk`,
`3ea2a6e5` on #776's branch) and its measurements are in [the
record](https://github.com/buster14a/buster-performance/tree/e5cc5bb8b3a2265772611d45a502dcdac8d34e94/docs/measurements/units-functions-777-cache-776-2026-10-29). One executable without the walk and with it, on the
9700X's CPUs 0 to 7, by 8 lanes (whole processes' medians, `time-runs`; the
stages are `--time-stages`' medians of three runs):

| ms | without | with the walk | the walk | the lanes' run, without and with |
| --- | ---: | ---: | ---: | ---: |
| Lua's 33 units, linked | 31.35 | 32.27 | 3.47 | 27.27, 25.47 |
| synth's 512 units, linked | 527.75 | 602.13 | 21.20 | 498.87, 540.67 |
| musl's 1,006 units, `-c` | 235.01 | 243.15 | 4.41 | 209.37, 209.83 |

- **What the units saved** is less than the walk takes: Lua's lanes ran 1.8 ms
  shorter with their files prepared, for a walk of 3.5 ms, and musl's saved
  nothing, its lanes' own caches preparing each header once after their first
  units and its units' own files small.
- **Synth's lanes ran 42 ms longer:** the walk prepared every unit's main
  file at the start (62 MB of source, 12.9 million tokens), which a unit then
  read from memory long out of the caches, where a unit that prepares its own
  file reads its tokens as they are made.
- **The peaks rose** by the walk's records, which every lane's copies borrow
  until the last unit: by 18 MB for Lua, 87 MB for synth and 60 MB for musl.
- **On fewer lanes** the walk costs less, and saves no more: Lua 46.05 and
  46.93 ms by 2 lanes, 76.28 and 76.40 by one; synth 917.3 and 935.8 by 2,
  1,629.1 and 1,620.0 by one; musl 387.9 and 390.9 by 2, 641.4 and 638.8 by
  one.

The walk alone, from nothing (`benchmark-files` by 8 lanes), takes 1.98 ms for
Lua's roots (242 files, of which 47 no unit entered, 8.5% of their tokens),
3.27 ms for musl's (1,221 files, all entered) and 20.19 ms for synth's. A
cache that the lanes fill as they run, with a lock for its writers (#776's
other way), wasn't built, and these runs don't measure it: it would have
neither the walk before the units, nor the lanes' copies, nor the walk's
tokens read long out of the caches, and would pay for its lock's waits and
its shared memory's traffic instead. The 1.8 ms Lua's lanes saved is what the
walk's prepared files saved them net of those copies and cold reads, not a
bound on what such a cache could save.

### Diagnostics

Each unit writes its diagnostics, the preprocessor's (`PPOptions.diagnostics`)
and the driver's own, to a stream of its own (`open_memstream`). Once every
unit has run, the calling thread prints them in the command line's order.

### Memory

A unit's compile reserves about 70 arenas and releases them (2,263 `munmap`,
2,282 `mmap` and 4,190 `madvise` calls for Lua's 33 units): the preprocessor's
alone reserves about 50, a column or a table each, and the parser, the type
table and the emission theirs. A command of one file pays for that once. A
lane that compiles units one after another pays for it per unit, and that is
what limited the lanes, as measured on the 9700X with synth's 512 units and
musl's 1,006 (`-c`, CPUs 0 to 7):

- **Huge pages.** Past an arena's first 64 KB, its pages are huge
  (`lane.c`'s `BUSTER_ARENA_SMALL_BYTES`), and the kernel clears a huge page
  whole when it is first touched: a small unit's array that goes a few
  kilobytes past 64 KB clears 2 MB. On 8 lanes those clears took 23% of the
  samples (`clear_page_erms`, under `pp_run_contexts`, `check_input_bind`,
  `check_program` and others), and their memory traffic tripled the user
  time: 1 lane took 1.63 s, 8 lanes 0.97 s. With transparent huge pages off
  for the process (an `LD_PRELOAD` of `prctl(PR_SET_THP_DISABLE)`), 8 lanes
  took 0.62 s and 1 lane the same 1.63 s.
- **The address space.** Every `munmap` flushes the TLBs of every CPU the
  process's other lanes run on, and `mmap`, `madvise` and `munmap` take the
  address space's lock for writing, which the other lanes' page faults and
  reservations then wait for. For synth, `osq_lock` and
  `rwsem_down_write_slowpath` took 4.4% of the samples, and eight processes of
  64 units each, which share nothing, took 0.46 s with huge pages off, where
  one process of 8 lanes took 0.62 s. musl's units are smaller (0.74 ms each
  on one lane), and their reservations come closer together: on 8 lanes 83%
  of the samples were the kernel's, 45% `osq_lock` under
  `rwsem_down_write_slowpath` (18% from `madvise`, 16% from `munmap` and 10%
  from `mmap`), and 8 lanes took 429 ms, more than 4 (413 ms), with 2.2 s of
  system time against one lane's 0.3 s.
- **The machine.** Even those eight independent processes reach 3.5 times one
  lane's throughput, not 8: their user time is 2.3 times one lane's, the
  compile's memory traffic (#151's measurements: 8 one-lane compiles of
  `sqlite3.c` reach 2.9 times one's, at 268.73 bytes of DRAM traffic per
  source byte).

So a units lane does four things:

- **It keeps its three big arenas** (the driver's arena, scratch and output,
  which the checker and the emission fill) from one unit to the next, emptied
  after each unit with their pages kept. Reserving them again for each unit,
  as a command of one file does, made the 8 lanes take 0.83 s rather than
  0.62 s (with huge pages off), and clearing what each unit pushed into them
  (`memset` to their peak) was slower still: a unit's pushes there reach far
  past the bytes it writes. The next unit finds the bytes the last one left,
  and the compile reads nothing in them it didn't write: `--poison-arenas`, a
  test option, overwrites them with a pattern between units (`arena_poison`),
  and `test-check-units` requires every object and executable it makes so to
  be the one each unit makes alone.
- **It pools the arenas its units release** (`arena_pool`, which only the
  units' lanes set): `arena_release` frees an arena's pages with
  `MADV_DONTNEED`, which takes the address space's lock for reading and leaves
  the mapping and its advice in place, and keeps the arena; the next
  reservation of its capacity and base pages takes it, its pages zero again as
  in a fresh arena, so that nothing depends on what the units before left. A
  full pool (256 arenas) unmaps the one released longest ago: some arenas'
  capacities follow their unit's size, and no later reservation takes them.
  On the laptop (Zen 4), c-testsuite's programs five times over (1,100 units,
  `-c`) took 0.81 s on one lane and 0.65 s on 8 without the pool, and 0.51 s
  and 0.33 s with it; on 8 lanes, its `mmap` calls fell from 74,446 to 12,686
  and its context switches from about 100,000 to 43,000. Pooling the three big
  arenas too, rather than keeping them, made the same units take 0.64 s and
  0.38 s: their pages are cleared again for each unit.
- **A small unit's other arenas start with base pages** (`arena_small_floor`,
  which only the units' lanes set): the first 16 MB of each arena that a unit
  of less than 1 MB of source reserves (`BUSTER_CHECK_UNITS_SMALL_BYTES` and
  `BUSTER_CHECK_UNITS_LARGE_SOURCE`), so that its small arrays take base pages
  where each took a huge page. With 1, 4 and 16 MB of base pages, synth's 512
  units (174 KB of source each) took 0.80, 0.73 and 0.65 s with `-c` on 8
  lanes, and 1.60 to 1.63 s on one, whatever the floor (the medians of three
  runs). A large unit's arrays fill their huge pages, and base pages only
  give it more faults: `sqlite3.c` (9 MB) with a tiny second unit took 0.127,
  0.131 and 0.136 s on one lane with those floors, and 0.124 s alone. So a
  unit of 1 MB of source or more reserves its arenas as a command of one file
  does.
- **It releases its memory itself** (#778): once it takes no more units, the
  lane releases its intern table, its file cache and its three arenas into its
  pool, then unmaps the pool's arenas, their pages freed already. So the lanes
  free their memory at once, `MADV_DONTNEED` taking the address space's lock
  for reading, where the calling thread released every lane's after them,
  alone: on synth's link by 8 lanes, that release took 7.5 to 8.2 ms, and the
  calling thread's release after the lanes now takes 0.6 to 0.7 ms.

## The link

A unit that links keeps its link (`check_units_keep`): its code and data as
`-c` would write them (its emission without binding, its static data
published, its definitions in named sections given their classes, #619's
`check_program_sections`, and `link_check` passed), copied into its lane's
arenas with every record's id as it was, and which of its undefined global
symbols a system header declared, for the error that names their library. Its
other memory goes back to the lane for the next unit.

Once every unit has run, `check_units_merge` joins the units' links into one,
in the command line's order, as the emission builds one unit's
(`check_program_emit`):

1. **Symbols.** Each unit's symbols in its order. A global name is one symbol
   for all the units, found in a table of the names' hashes; a local symbol is
   its unit's own. A name that two units define is an error ("multiple
   definition of 'x'"); a weak definition yields to a strong one. The symbol
   takes its definition's flags, the most constraining visibility any unit
   gives it, and for a reference a weak binding only when every unit's
   reference is weak. The units' definitions are the program's
   (`BUSTER_LINK_SOURCE_PROGRAM`).
2. **The runtime.** `bind_begin` joins the C runtime between the program's
   symbols and its atoms, its units numbered after the program's.
3. **Atoms.** Each unit's atoms in its order, in a unit of the link of their
   own (so that `link_check`'s rule that a local symbol is referred to only from
   its own unit holds), each with its fixups, their targets the merged
   symbols; then its priorities and its definitions.

The link's room for the runtime's records is `bind_capacity_needed`'s for the
names no unit defines. Then `check_driver_executable` binds and writes the
executable as it does one unit's: `bind_end`, `link_check`, `dynamic_inputs`
and `executable_write`, with the same errors, and once more from a fresh
runtime when the snapshot is damaged.

### On the units' lanes

On more than one lane, the vector path merges the units' links on the units'
lanes (`check_units_merge_lanes`, #778) into the same link, every record with
the same id. `check_units_merge` is its exact twin, which one lane and
`--scalar` run, and which the merge leaves the work to when its memory or its
lanes can't be had.

1. **The global names** are rule 4's exchange ([the linker's
   design](object-writer-and-linker.md), "Lanes"). Each lane takes a share of
   the units, by their symbols (`lane_range_by_cost`), counts their global
   symbols by partition of their names' hashes (64 partitions per lane, a
   power of two), and places them, partition by partition in the lanes'
   order, so that each partition holds its symbols in the units' order. Then
   each lane resolves the partitions it takes (`lane_take`), in a table of each
   one's names, as the twin resolves them one symbol at a time: a name's first
   symbol stands for it, with its definition (the first strong one, else the
   first weak one), its first duplicate definition, its most constraining
   visibility and whether any symbol binds it otherwise than weakly.
2. **The ids.** Each unit's locals and the global symbols that are their
   names' first take the link's next ids, in the units' order (each unit's
   count, a prefix sum, then each lane its units'), and every other global
   symbol its name's first's.
3. **The records.** The symbols and their names' bytes, each lane a range of
   the ids, the bytes at places that a prefix sum of the names' lengths gives;
   then each unit's atoms, fixups (their wide addends numbered as
   `link_add_fixup` numbers them), priorities and definitions, at places that
   prefix sums of the units' counts give, each lane its units by their
   records.

Between those steps lane 0 alone takes what the twin decides in its order: the
first duplicate in the units' order (the least of each partition's first),
which it reports as the twin does; the names no unit defines, which give the
runtime's room; `bind_begin`; and `-g`'s DWARF. The kept links passed
`link_check`, so their records are copied as they are, their ids remapped, and
the bound link's `link_check` runs on the same lanes (`link_check_lanes`).

A unit's code reaches the symbols other units define as it reaches any it
doesn't define: through the GOT, which the executable's write relaxes for
the image's symbols (`call` to `addr32 call`, `mov` to `lea`). Binding every
external name before instruction selection, which a link of several units
makes possible (`docs/object-writer-and-linker.md`, "Binding before
instruction selection"), is a later step.

### The symbol table

The direct path's executables now carry the program's symbols in `.symtab`
(`ExecutableOptions.symbols`), as a linker writes its objects': every named
symbol of the program's but the compiler's `.L` labels, which an assembler
leaves out of its objects, and a TLS variable of no size, which the writer
has no section for. With `-g`, the DWARF units' function symbols stay what
they were. So `nm` and `perf` name an executable's functions, and
`test-check-units` compares the symbols an executable of several units
defines with those of the units' objects linked by `cc`.

### The DWARF

With `-g` (#779), each unit builds its DWARF on its lane as `-c -g` writes it
into the unit's object (`c_check_debug.c`'s unit, written alone: its four
fields that point into its other sections, the header's abbreviation offset
and the unit's line program, address base and range lists, hold offsets in
its own sections), and keeps it with its link (`CheckUnitsKept.debug`, an
`ObjectDebug`): its five sections' bytes, its functions' symbols and line
addresses, and where its fields are. The merge
(`check_units_merge_debug`) lays the units' contributions out one after
another in the command line's order, adds to each unit's fields where its
contributions start, as a linker's relocations against the sections do, maps
its functions to the merged link's symbols and moves its line addresses by
where its `.debug_line` starts; the executable's write takes them as units
(`ExecutableDebug.unit_functions`), each finalized at its functions'
addresses. A weak function that another unit's definition replaces keeps its
code, which its unit's DWARF describes: the merge gives it a local symbol of
its own at that code, after the program's other symbols, as a linker relocates
an object's DWARF against its section rather than its name (the checker
refuses the weak attribute until #758).

On the 9700X, `-g` costs Lua's 33 units linked by 8 lanes 1.3 ms (35.5 ms
against 34.2) and by one lane 5.1 ms (83.5 against 78.4): whole processes on
CPUs 0 to 7, the medians of 15 runs, at #779's commit (`520ba859`), whose
commands without `-g` run as #763's head's; [the
record](https://github.com/buster14a/buster-performance/tree/d3ed81314e41efdf56e776e8c0ba1bee2efb0fec/docs/measurements/units-tail-778-779-2026-10-29)
has the commands and the logs.

## Measurements

On the 9700X, at `ae5fb51e`, built as the 9700X job builds
(`BUSTER_DEBUG_INFO=0`), each command a whole process on CPUs 0 to 7 (one
per core), the median of 5 runs after a warm-up, in an order that rotates by
run (`time-runs`); [the record](https://github.com/buster14a/buster-performance/tree/5dbccd1e1f5ce39235835328d020aed730d937ba/docs/measurements/units-152-2026-10-29) has the commands, the logs and the
host's receipts. Buster's commands compile the units with `--lanes N`; make's
compile the same units, a process for each, and link as buster does (Lua and
synth). Lua 5.4.8's interpreter is its 33 units but `luac.c`; synth's shape
is `test-check-units --generate`'s 512 units of 200 functions (89 MB); musl
1.2.5's units are the 1,006 of its library's 1,309 that the driver compiles
(it refuses the other 303 as unsupported).

| milliseconds | buster, 1 lane | 2 | 4 | 8 | `make -j8`, clang -O0 | `make -j8`, tcc | `make -j1`, tcc |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Lua, linked | 76.1 | 45.8 | 32.4 | 30.9 | 141.2 | 18.6 | 86.4 |
| synth, linked | 1,546.6 | 884.9 | 607.5 | 532.7 | 2,482.9 | 507.6 | 1,503.5 |
| musl, `-c` | 625.5 | 381.7 | 260.4 | 232.7 | 1,675.7 | 149.4 | 979.3 |

8 lanes are 2.5 (Lua), 2.9 (synth) and 2.7 (musl) times as fast as one.
clang -O0 on 8 processes takes 4.6, 4.7 and 7.2 times as long as the 8 lanes,
and tcc on 8 processes 0.60, 0.95 and 0.64 times as long; on one lane, the
driver takes 0.88, 1.03 and 0.64 times as long as tcc's processes one at a
time.

What the 8 lanes are short of:

- **The serial tail.** Synth's link spends 485 ms of 533 on the units' lanes
  (`--time-stages`' `lanes`; 1,510 ms on one lane), then 19 ms merging the
  units' links (`code-atoms`) and 25 ms binding them and writing the
  executable, as on one lane: 8% of 8 lanes' time. The merge stays scalar, a
  hash table of the global names, 3.6% of it. #778 since halved the merge and
  the release after the lanes ("The link's tail" below).
- **The memory.** Synth's user time is 1.27 s on one lane and 3.13 s on 8, as
  eight independent processes' grew (above), and its system time 0.27 s and
  0.68 s (0.35 s and 1.33 s before the pool).

Before the arenas' pool (`965ec185`), the same commands took 34.6, 687.7 and
429.3 ms on 8 lanes (80.3, 1,635.9 and 741.7 on one), musl's 8 lanes more than
its 4 (413.4 ms).

A command of one file runs as on main (`9bb5060e`), against an A/A copy of
main's executable: `perf stat -r 11` on CPU 2, the medians of three rounds in
a rotating order, of the frozen `sqlite3.c` (9 MB) and of a c-testsuite
program linked by the direct path.

| | main | main's copy | head | head against main |
| --- | ---: | ---: | ---: | ---: |
| `-c`, user instructions | 1,243,623,074 | 1,243,623,098 | 1,244,367,336 | +744,262 (+0.06%) |
| `-c`, user cycles | 574.39 M | 575.12 M | 573.80 M | -0.10% (copy +0.13%) |
| `-c`, task clock | 121.32 ms | 121.27 ms | 120.86 ms | -0.4% |
| `-fsyntax-only`, user instructions | 946,882,976 | 946,883,627 | 947,056,569 | +173,593 (+0.02%) |
| `-fsyntax-only`, user cycles | 439.45 M | 437.81 M | 440.02 M | +0.13% (copy -0.37%) |
| `-fsyntax-only`, task clock | 95.50 ms | 95.19 ms | 95.50 ms | 0 |
| executable of `00220.c`, user instructions | 21,413,378 | 21,413,398 | 21,434,532 | +21,154 (+0.10%: `.symtab`) |

The object is main's, byte for byte. Placement moves these instruction
counts by as much as they differ: on the laptop (Zen 4), whose head's counts
are main's +157,000 and +727,000, a build of the head without `arena_push`'s
peak, its one addition to every push, ran 1.1 and 0.6 million more
instructions than the head, not fewer.

### The link's tail

#778's changes (the merge and `link_check` on the units' lanes, the lanes'
own release, one team), measured on the 9700X at its head (`3c615fda`) and
recorded with #779's in [the
record](https://github.com/buster14a/buster-performance/tree/d3ed81314e41efdf56e776e8c0ba1bee2efb0fec/docs/measurements/units-tail-778-779-2026-10-29).
Synth's 512 units linked by 8 lanes, `--time-stages`' medians of three runs,
against the stage split alone (`ccad85a6`), the two executables in turn:

| ms | the split alone | #778 |
| --- | ---: | ---: |
| `code-atoms`: the units' merge | 18.48 | 9.57 |
| `executable`: binding, `link_check` and the inputs | 4.88 | 1.21 |
| `write` | 11.38 | 10.99 |
| `release`, after the lanes | 8.97 | 0.64 |
| `total` | 532.93 | 510.72 |

- **The merge** on 8 lanes takes about half its twin's time on one (19.69 ms):
  lane 0's steps between the lanes' (the first duplicate, the runtime's room,
  `bind_begin`) and the copies' memory traffic are most of it.
- **`link_check`** on the lanes took the `executable` stage from 4.9 to 1.2 ms.
- **The write** is its chunks' (`time-write`'s `chunks`, 10.1 ms of 11.0):
  the page cache's copy of the 44 MB output, which stops scaling at three
  lanes on this file system ([linker scaling](linker-scaling.md)).
- **The release** is the lanes' own now, in `lanes`, overlapping their other
  work. On one lane it moved there whole: `lanes` took 5 ms more, `release`
  0.27 ms rather than 4.95, the same total.

`runtime` took 3.4 to 4.2 ms in every run of that comparison, as each
executable found the other's runtime snapshot (a snapshot serves the
executable that wrote it) and imported the runtime again; one executable's
runs after the first take 0.1 ms.

As whole processes on CPUs 0 to 7 (`time-runs`), against #763's head
(`29770b85`): synth's link by 8 lanes took 518.1 ms (513.4 to 527.9, 7 runs)
against 531.4 (529.0 to 541.4), and by one lane 1,547.0 against 1,546.7;
Lua's link 34.3 ms against 34.9 by 8 lanes and 78.1 against 78.8 by one (15
runs, whose ranges overlap); musl's `-c` by 8 lanes 231.8 ms against 231.5
(21 runs). A command of one file runs as #763's: `perf stat -r 11` of `-c
sqlite3.c`, `-fsyntax-only sqlite3.c` and a c-testsuite program's link
retired 7,800 to 7,900 fewer user instructions of 1.23 billion and 933
million (212 of 21 million), with cycles inside an A/A copy's spread, and the
object is the same bytes.

## Tests

`./build/compiler test-check-units [--executable PATH] [--clang PATH]
[--seed N] [--programs N] [--keep] [--corpus DIR [--option ARG]...]` runs the
driver as a process, in a directory of its own:

- **Programs:** generated programs of 2 to 7 units, whose functions call
  other units', read and write their globals (initialized, zero-filled and
  thread-local), take their functions' and globals' addresses into tables and
  pointers, pass structs by value, call `printf`, have statics and static
  functions of the same names as the other units', and a function and a
  zero-filled global in sections of their own names (#619); and one of synth's
  shape (12 units of 30 functions). Each program's executable of all its units, by
  `--lanes` 1, 2, 3 and 8, by default and by 8 with `--poison-arenas`, and by
  each path, is the same bytes, runs as `clang -O0`'s build of the program
  does, and defines the symbols (by name, binding and kind) that `cc`'s link of
  the units' objects compiled one at a time defines; that link runs as clang's
  too. Each unit's object of `-c` of all the units is its object alone, at
  every lane count and poisoned. With `-g`, the executable is the same bytes
  by `--lanes` 1 and 8, by 8 poisoned and by the scalar path, and runs as
  clang's build does; and each unit's object of `-c -g` is its object of
  `-c -g` alone. `test-check-debug --cross-check` links two units with `-g`
  and reads them with `llvm-dwarfdump --verify` and gdb: each unit's functions
  and lines in their own files, the static functions of one name apart, and
  backtraces across the units; and a third unit's weak function that the
  second's replaces, at its own code (once the checker takes `weak`).
- **Diagnostics:** a unit with a warning, one with an error and a clean one,
  with `-c`, `-fsyntax-only` and into an executable, at every lane count:
  standard error is each unit's alone, in the command line's order; `-c`
  writes the objects of the units without errors only, and the link writes
  nothing.
- **Refusals:** a name two units define, a name none defines, `-c -o`, a dump,
  `--id-order`, two units of one object's name; and that last one in a child
  whose stack was filled with a mapping's address first, which must exit with
  its message as with a clean stack: a local the driver read before setting
  it would release an arena of that address and unmap the child's code.
- **A few large units** (#777): a unit of 800 functions beside a small one's
  `main`, with `-c` by 8 lanes and `--shared-parts` (a test option: a sharing
  emission pauses after each part it takes itself, and the command reports how
  many parts the other lanes took), each object its own alone, the six lanes
  without a unit having taken parts of the large one's; and linked by
  `--lanes` 1, by 8 and by the scalar path on 8, the same bytes, a program
  whose sum is the test's.
- **The merge on lanes against its twin**, by `--lanes` 1 (the twin), 2 and 8
  and by the scalar path: four units that define three names twice, refused
  with the same standard error, which names the first duplicate in the units'
  order (`beta`, which the third unit defines again, though `alpha`'s first
  definition comes first) and its first definition's unit; and units of a
  pointer whose addend doesn't fit 32 bits (a wide addend) and of no symbols,
  linked into the same bytes, a program in which the pointer holds its addend.
- **Reservations:** two units that include one header, with `-c` by one lane,
  in children whose address space may grow a page less than a run reached (as
  `test-parser` fails its driver's): the run's last reservation or mapping
  fails, then, a page below the peak that run reached, the one before it, down
  to the first. Each run must exit with status 1 having said why in an
  error, or, where the reservation has a fallback (the emission's columns,
  which take the caller's scratch then), write the same objects as without a
  limit; and either way leave the address space and the descriptors as they
  were. Among them a lane's setup fails at its reservations (and says so:
  `can't reserve the lane's memory`), its file cache's after its intern table
  and the cache's first arrays were reserved, which the lane releases with the
  rest.
- **The jobserver:** four units by `--lanes 4` under a GNU make jobserver of
  three tokens, linked into an executable of more than a megabyte whose path
  is a FIFO, which the test reads: while the writer writes, the jobserver
  holds none of the tokens; once the command ends, it holds every one again,
  and the executable runs.
- **Lanes that don't start**, in a child limited to the processes it has:
  one lane's outputs.
- **`--corpus DIR`:** every `.c` file in DIR at once with `-c`, by 8 lanes,
  plainly and with `--poison-arenas`, against each file alone: the same
  statuses, diagnostics and objects. The 9700X job runs it on c-testsuite's
  programs.
- **`--generate DIR`** writes a program of synth's shape (`--units`, 512 by
  default, of `--functions`, 200) for measurements, and tests nothing.
