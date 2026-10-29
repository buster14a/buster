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
on its lane alone. #151's function lanes inside a unit's lane would be a lane
group inside another, which `lane_run` refuses, and with several units the
units fill the lanes: on the 9700X's eight cores, more than a few units keep
every lane busy.

The driver runs at most one lane per unit, and by default one per CPU the
process may run on; `--lanes N` runs N, and the worker budget (#673:
`--worker-budget`, GNU make's jobserver) limits the lanes past the calling
thread's own as it limits #151's. Its lease covers the units' lanes and the
executable's writers, which run on as many, and its slots go back once both
are done. Lanes that can't start leave their units to the calling thread,
which compiles them alone: the same outputs.

Before the lanes start, the calling thread builds the tables every unit's code
reads and the first unit to need them would otherwise build: the parser's
(through `parse_initialize`, in the front end's home: `main.c`), the
encoder's, the semantics' and Tier 0's, and with unwind tables, theirs. The
lanes then only read them.

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

### Not shared across lanes, yet

#152's design shares one file cache and one intern table between all the
units, so that a header is prepared once in the process, as #150's run-ahead
walk prepares a command's files on lanes. A unit's preprocessing writes both:
the spellings that token pasting makes, the paths its `__has_include` and
computed includes probe, and a file the walk didn't prepare (pp_run's
`intern_find_or_add`, `file_cache_lookup_path` and `file_cache_prepare`). One
cache for units running at once on several lanes needs those writes made
safe for readers on other lanes: a lock for the writers and tables that never
move while lanes read them, or each lane's writes into an overlay of its own.
Each lane's cache prepares a header at most once per lane instead: on 8
lanes, a header that every unit includes is prepared 8 times rather than
once.

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

So a units lane does three things:

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
  hash table of the global names, 3.6% of it.
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
