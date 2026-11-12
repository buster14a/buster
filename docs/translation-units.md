# Many translation units in one process

## Selectany group selection

The C emitter publishes external `selectany` definitions in the shared
`LinkGroups` format, after their atoms and symbols are complete. The object
writer consumes it directly. `check_units_keep` copies each member atom's
signature into the unit's immutable kept records for direct linking.

Before resolving names, `check_units_merge_lanes` reuses the stable hash
partitions of its global-name exchange to select each signature's first group
in command-line order. Each unit independently marks retained atoms and
references, then compacts its atom ordinals. The ordinary name-resolution pass
therefore sees no definition or reference contributed solely by a discarded
group. The serial twin uses the same selection rule with its existing name
table. A reference from a retained atom to a discarded local is diagnosed;
references to nonlocal symbols resolve through the surviving name.

Selection arrays are absent when no unit has groups. Grouped commands allocate
one atom map and symbol mask per unit, with no borrowed scratch pointers in the
kept representation. Once selection is complete, the final executable's link
needs no group table. Runtime binding may add atoms after that point.

## Driver

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
- `--dump-typed` and `--dump-static` take one source file.
- `--id-order N` primes each unit's lane-owned intern table with unused and
  reordered names before its real compile, as in the single-file driver.
  Objects, executables and diagnostics must match an unprimed command.

Every unit is compiled whatever the others do, as clang compiles them: a unit
with errors writes no object, and the others write theirs; any unit's error
makes the exit status 1 and leaves no executable. The diagnostics come in the
command line's order, each unit's exactly as it alone would print them, through
the first unit with a fatal diagnostic. Every unit finishes before publication,
so a fatal on a fast lane cannot cancel an earlier unit's diagnostics. The
fatal unit's notes and summary remain attached; later units' buffers are freed
without printing them.

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
functions' parts (#777, below); by default no more than one per core of the
CPUs the process may run on (#905: its session's lane 1 counts them,
`BUSTER_LANE_SESSION_PER_CORE`), and with `--lanes N` no more than N, at most
one per CPU; and the worker budget (#673:
`--worker-budget`, GNU make's jobserver) limits the lanes past the calling
thread's own as it limits #151's. Each of the command's wide parts takes its
own lease and gives its slots back as it ends, before any serial work: the
units' run, and the executable's merge, `link_check` and write, which want as
many lanes as the units' run took. A unit no lane took compiles on the calling
thread alone: the same outputs.

If the parallel merge cannot reserve its scratch workspace, it pops that
workspace and returns the merge's worker-budget lease before calling the serial
twin. The caller's final return remains idempotent; an early return failure is
sticky in both the budget and the flow and still fails the command.

The command's lanes are its session's (#905, `lane_session_run`, as
`link-foreign`'s), created once after its options and before any source is
read, and every lane runs the command's flow (`CheckUnitsFlow`, a `LaneFlow`
of `lane.c`'s): lane 0 runs the driver alone, and each wide part (the units'
run, the merge of their links, its `link_check` and the executable's write) is
a round, which lane 0 announces with its kind and lanes and every lane runs,
the first of them its operation by a direct call: lane 0 between
`lane_flow_round_begin` and `lane_flow_round_end`, the others by its kind
(`check_units_follow`), so that nothing a unit's compile calls leads back to
the units' run, as the call graph check requires. The lanes keep their threads
and scratch arenas from one round to the next rather than starting and
joining threads of their own (#778). The units' lanes are at most the
session's: one when its lanes couldn't start, and then lane 0 runs every
round alone.

Before the lanes start, the calling thread builds the tables every unit's code
reads and the first unit to need them would otherwise build: the parser's
(through `parse_initialize`, in the front end's home: `main.c`), the
encoder's, the semantics' and Tier 0's, the lowering's runtime functions'
names and the asm matcher's index (#2532), and with unwind tables, theirs,
where the executable isn't baked with them. The lanes then only read them.

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
ticket takes no part of the next. Each part finishes every access to the run
before incrementing the done count. The last part publishes the epoch complete,
which the sharing lane always acquires before its emission goes on or it opens
the next epoch: reaching the done count alone can precede that publication.
Thus no lane reads an epoch's run after its emission, and no previous epoch's
completion can overwrite the next's. An emission that starts while
every lane still takes units runs alone, its bodies one part, as before: a
command of many units pays for the board only once its units run out, when its
last units' emissions share their parts.

On the 9700X's CPUs 0 to 7, SQLite 3.53.4's `sqlite3.c` (9.5 MB) and `shell.c`
(1.2 MB) as one command took 122.1 ms with `-c` and 126.0 ms linked once the
board came, against #778's 133.9 and 138.3 ([that
record](https://github.com/buster14a/buster-performance/tree/afdfdd4051b834e5a880766a87bf7f464be3b437/docs/measurements/units-functions-777-cache-776-2026-10-29)).
Its control, the units' two processes at once, was waited for without checking
the processes' statuses or outputs, and its comparison with them qualifies
nothing.

The control that does ([the record](https://github.com/buster14a/buster-performance/tree/4745e2bdceac469c1b3557ce012255fb0c9a8186/docs/measurements/units-control-777-2026-11-02)):
the one command against its units' two processes at once by the same
executable, each process's exit status checked and every output removed
before each run and required after it, the whole processes' medians of 31
runs (`time-runs`), every object its unit's alone and every executable main's.
At `3ce71ec3` the one command took 114.37 ms with `-c` against 113.79 for the
processes, and 116.34 linked against 129.84 with `cc`'s link (21 runs). With
the lanes' release of #777 (below), at `8dad76a2`, against main at `b70deb47`,
which has the pool of #1128 (#1158) that the release goes through, in two
rounds:

| ms | main | the lanes' release | two processes at once |
| --- | ---: | ---: | ---: |
| `-c` | 111.63, 111.33 | 110.62, 111.29 | 111.48, 111.22 |
| linked | 116.09, 117.05 | 116.08, 116.03 | 119.10, 119.45 with mold's link; 128.40, 128.95 with `cc`'s |

- **The units grow alike in one process and in two.** At `3ce71ec3`, beside
  each other, `sqlite3.c`'s preprocessing takes 11.9 ms against 9.9 alone,
  its `check-input` 52.8 against 48.7, and `shell.c`'s `check-input` 23.8
  against 15.8, in the one command (medians of 7 runs of a trace of its
  lanes, in the record's `experiment/`) as in the two processes (11.8, 52.4
  and 23.7 by `--time-stages`): what the units share is the machine's memory,
  not an address space. So the one command can match its processes, and beat
  them only where it does less.
- **The one command's tail.** At `3ce71ec3`, the lanes' run ended 1.4 to
  1.7 ms after `sqlite3.c`'s unit, 110.7 ms into it: lane 0 released its
  memory (1.0 to 1.4 ms), and `shell.c`'s lane, done 45 ms in, released its
  own (0.9 to 1.7 ms) only then, last in 6 of the 7 runs. A lane now releases
  its memory into its pool as soon as it has no unit left to take, and
  without a link the last lane leaves its memory to the exit ("Memory"
  below): the one command is 0.8% faster and 0.1% slower than its processes
  in the two rounds, and 0.9% and 0.03% faster than main, which was 0.1%
  slower than the processes in both. Its peak is 381 MB rather than 435
  (386 linked, rather than 444); the two processes peak at 337 MB, the larger
  one's. Before #1158, in six rounds at earlier mains (the record), it ranged
  from 0.7% faster to 0.3% slower than its processes.
- **Linked**, the one command is 2.5% and 2.9% faster than its processes and
  mold's link of their objects, and 9.6% and 10.0% faster than with `cc`'s.
- At `3ce71ec3`, the large unit's emission shares its parts with the seven
  other lanes, which take 1,019 of them: its `lanes` stage takes 4.1 ms,
  against 4.7 in a process of its own beside `shell.c`'s and 4.9 alone
  (medians).
- Its lanes without units sleep on the board's futex at once: waiting with
  `lane_wait`'s spin and its millisecond in `mwaitx` took 192 ms of user time
  against 164 to 171, with no difference in wall time (#834's record).
- Lua's, synth's and musl's commands run as main's at `b70deb47`: by 8 lanes,
  Lua's link takes 33.06 ms against 34.14, synth's 467.7 against 471.6 and
  musl's `-c` (its 1,309 units) 215.2 against 216.3; by one lane, 75.58
  against 76.42, 1,656.7 against 1,661.3 and 862.6 against 865.0. Their peaks
  are main's, from 0.6 MB below to 2.9 MB above.

#### Against #763's build

#777's "Done when" compares the commands of #763's record with #763. Its
record's figures can't stand for #763 any more: on the same host, #763's
merge (`3ba1621f`), built from its commit, now takes 3 to 6% more by one
lane than its record says (Lua's link 80.45 ms against 76.1, synth's
1,632.7 against 1,546.6, musl's 645.1 against 625.5), and 1 to 12% more by
8 lanes. The comparison of 2026-11-03 with those figures included that drift.
So #763's build runs beside main's instead, in the same `time-runs`, on the
9700X's CPUs 0 to 7. Main is `ec5f0f6b` (2026-11-06), and the units' files
are main's at `f4272a46` too ([the record](https://github.com/buster14a/buster-performance/tree/44159e57007309273eef6561123cab71e9aede05/docs/measurements/units-recheck-777-2026-11-06)).

- **A few large units.** `sqlite3.c shell.c` by one command, in two rounds
  of 31 runs:
  - with `-c`, 138.55 and 138.24 ms, against 138.79 and 138.36 for its two
    processes at once;
  - linked, 142.86 and 142.93 ms, against 146.55 and 146.40 with mold's link
    of the processes' objects, and 155.77 and 155.63 with `cc`'s.

  Each process's status and outputs are checked, as in the control above.
  Every object is its unit's alone. The one command's peak is 359 MiB, and
  the larger process's is 338 MiB.
- **#763's commands** (musl's are the 1,006 units of #763's record), medians
  of 21 (Lua's) and 7 runs, with their `wait4` peaks in MiB:

  | ms (MiB) | main | #763 | main against #763 |
  | --- | ---: | ---: | ---: |
  | Lua, linked, 8 lanes | 33.20 (207) | 34.61 (213) | -4.1% |
  | synth, linked, 8 lanes | 484.1 (547) | 545.6 (530) | -11.3% |
  | musl, `-c`, 8 lanes | 148.5 (239) | 235.8 (249) | -37.0% |
  | Lua, 1 lane | 98.74 (61) | 80.45 (74) | +22.7% |
  | synth, 1 lane | 1,780.8 (318) | 1,632.7 (327) | +9.1% |
  | musl, 1 lane | 689.1 (142) | 645.1 (154) | +6.8% |

- **By one lane, each unit's compile is what grew, not the units' path.**
  The same units, compiled one process each, one after another on CPU 2, are
  faster at main than at #763 (`-c`: Lua 168.9 ms against 188.7, synth 2,957
  against 3,598, musl 1,579 against 2,587). A process now starts with 0.8
  million instructions rather than 12.6 (a tiny unit's `-c`). Without that
  start, the processes' compiles retire more instructions than #763's:
  - Lua: 0.36 billion more, where its one command retires 0.33 billion more;
  - synth: 1.87 billion more, against 1.94;
  - musl: 1.99 billion more, against 1.27.

  The processes' `--time-stages`, summed over the units, put the growth in
  two stages, with Tier 0 third on synth's (+29 ms):
  - `check-input`: +12, +31 and +31 ms (#1489);
  - `check-program`: +11, +69 and +54 ms. That is #1049's common warnings,
    #1340.

  Each one command's objects are its processes' bytes, at both commits.
- **The units' path's own cost was the scratch's split huge page.** With
  `-w`, which skips the common warnings, synth by one lane still took 6.2%
  more user cycles at main than at #763 for 1.2% more instructions, and 16%
  more page faults. A unit's checker gave back its scratch's pages, so the
  lane's next unit faulted them in again; the huge page at the peak, split
  by that discard, came back a base page at a time ("Memory" below). With a
  lane's discard in whole huge pages (`559ca5f6`, whose code this is), on
  the 9700X, against main and #763:

  | ms (MiB) | the change | main | #763 |
  | --- | ---: | ---: | ---: |
  | synth, 8 lanes | 481.1 (556) | 491.9 (551) | 548.1 (530) |
  | synth, 1 lane | 1,725.1 (321) | 1,803.3 (321) | 1,634.9 (327) |
  | Lua, 8 lanes | 34.21 (218) | 33.84 (209) | 35.86 (212) |
  | Lua, 1 lane | 99.11 (65) | 99.68 (63) | 81.32 (74) |
  | musl, 8 lanes | 146.1 (242) | 147.1 (240) | 232.3 (248) |
  | musl, 1 lane | 688.5 (145) | 691.7 (145) | 641.2 (154) |

  - Synth's link took 575,099 faults by one lane against main's 704,539:
    4.3% less time by one lane and 2.2% less by 8.
  - Lua's and musl's links run as main's: Lua's 8 lanes are within their
    runs' spread (33.1 to 59.0 ms, and main's 32.9 to 50.0). In an
    experiment's build of main that switches between the forms, the whole
    huge pages took 29.28 ms against 30.40.
  - The cost is the huge page each lane keeps, at most 2 MB: the peaks by
    8 lanes are 1.7 (musl), 4.8 (synth) and 8.4 MiB (Lua) above main's.
  - The few large units run as main's, since neither has a unit left when
    it reaches the checker. With `-c` the one command takes 136.6 and
    136.9 ms against main's 137.9 and 137.7, and the processes' 137.7 and
    137.4. Linked, 142.2 and 142.0 against 142.2 and 142.6, and mold's
    145.3 and 145.3.
  - Every object and executable is main's.
- **Synth's peak by 8 lanes** is 551 MiB at main in a second run, 549 with
  the board disabled (`BUSTER_TEST_NO_UNITS_BOARD`), and 528 at #763. The
  board accounts for 2.5 MiB of it, and its executable is the same bytes
  without the board.

Against "Done when", then:
- the few large units' item holds;
- by 8 lanes, #763's commands are faster than #763's;
- by one lane they are slower, by each unit's compile, which #1340 and
  #1489 own;
- every object and executable is one lane's (`test-check-units` passed at
  main and at the change).

The change adds the huge page each lane keeps to the peaks by 8 lanes: 8.4
MiB to Lua's, which then is 5.8 MiB above #763's where main's was below it,
4.8 to synth's and 1.7 to musl's, which stays below #763's. Synth's was
already about 20 MiB above #763's at main, for a reason the record doesn't
find.

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

**What they don't share yet (#2531).** A header's declarations are read again
by every unit that includes it: on the 9700X at `ecaf57b5`, musl's 1,336 units
as one command read 297,004 file-scope declarations of 5,156 distinct texts,
and reading them is 32% of the command's cycles.
[Attested declarations](attested-declarations.md) is the design for that: a
declaration that an earlier unit of the lane read without a diagnostic, and
that the unit's own code doesn't reach by name, isn't read again. It has the
ranking of what else a command's units pay per unit (the kernel's page
cycle, the preprocessor, the asm matcher's index of #2532).

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

So a units lane does five things:

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
  be the one each unit makes alone. Since #442 it keeps its functions' lane 0's
  writer's columns too (`check_program_lanes_settle`, the lanes guide's
  "Memory"), which `--poison-arenas` overwrites with the others: each unit
  reserved them again, and faulted in the pages their columns' starts touch.

  The checker's last pass gives the scratch's pages past its columns back to
  the kernel (`arena_discard`, #628), which lowers one unit's peak. It stops
  at the peak's page, inside a huge page, which the kernel then splits into
  base pages: the rest of it, which no push wrote, stays resident, and in a
  lane the next unit's parse and reader faulted the freed part in again a base
  page at a time, unit after unit. So a unit whose lane has another to compile
  gives its pages back in whole huge pages (#777, `arena_discard_huge`): when
  a unit of the run is still to be taken (`check_units_discard_huge`, from the
  units the lanes have taken, `CheckUnitsRun.started`), its checker frees them
  to the last huge page boundary at or below the peak
  (`CheckProgramOptions.discard_huge`), which the next unit takes again in a
  fault each, and keeps the huge page that holds the peak, at most 2 MB a
  lane. Freeing that one whole too made each unit take a fresh huge page,
  which the kernel clears: synth's link by 8 lanes took 4.3% longer than
  main's discard (the record's "Freeing the peak's huge page whole"). A unit
  with none left after it, and a command of one file, discard as before.
  `--shared-parts` reports how many units gave their pages back in whole huge
  pages.
  The driver gives the scratch's pages back after the reader too, past 96
  bytes a token beyond its length (`check_driver_discard_scratch`,
  `BUSTER_CHECK_DRIVER_SCRATCH_REUSE`, #628): the stages up to the checker's
  last pass push that much of it again, and would fault it in again cleared,
  where they find it resident; a unit whose lane has another to compile
  gives them back in whole huge pages by the same test
  (`check_units_unit_left`), which `--shared-parts` doesn't count.
- **It pools the arenas its units release** (`arena_pool`, which only the
  units' lanes set): `arena_release` frees an arena's pages with
  `MADV_DONTNEED`, which takes the address space's lock for reading and leaves
  the mapping, its advice and its writable prefix in place, and keeps the
  arena; the next reservation that it holds takes it (the smallest pooled
  mapping with room for its capacity, its base pages and its page layout),
  with the capacity it asked for and its pages zero again as in a fresh
  arena, so that nothing depends on what the units before left. A full pool
  (256 arenas) unmaps the one released longest ago.

  The writable prefix and the closest fit are #1128's. Since #802 an arena
  commits its pages with `mprotect` as pushes reach them, and release made
  them inaccessible again, so that each unit committed its arenas anew:
  5,122 `mprotect` calls for Lua's link on one lane, each taking the lock for
  writing, and 4 lanes no faster than 8. Pooling by exact capacity also
  missed every arena whose capacity follows its unit's size (624 `mmap`
  calls). Now Lua's link on one lane makes 195 `mprotect`, 149 `mmap` and
  129 `munmap` calls, and on the 9700X (CPUs 0 to 7, the medians of 11 runs,
  main interleaved; [the
  record](https://github.com/buster14a/buster-performance/tree/77364f9c661a1d14e9cc7d95b66220bccaa6439b/docs/measurements/arena-pool-1128-2026-11-02))
  it takes 77.2 ms where main took 82.2 (1 lane), 48.1 against 52.2 (2), 35.3
  against 42.0 (4) and 33.2 against 41.9 (8): 6.1, 7.9, 16.2 and 20.7% less,
  and two earlier rounds measured the same within 1.6 points. Its system
  time on 8 lanes went from 100 to 75 ms, and synth's link on 8 lanes took
  508 ms where main took 616 (1,675 and 1,730 on one), with 814 ms of system
  time rather than 1,466. A command of one file reserves without a pool, as
  before: `-c` of the frozen `sqlite3.c` on CPU 2 ran 3,381 more instructions
  of 1,257 million, in the same time. Under strict overcommit or a finite `RLIMIT_DATA` release still
  makes the prefix inaccessible, returning its commit charge
  ([the arenas' guide](arena-memory.md)). Clearing a pooled arena's pushed
  bytes with `memset` on reuse, rather than discarding its pages, where they
  were at most 1 MB, was slower on 2 to 8 lanes (10.4% rather than 14.7% less
  on 4, with 6% more user cycles): the clears cost more than the faults and
  the TLB flushes they saved.
  On the laptop (Zen 4), c-testsuite's programs five times over (1,100 units,
  `-c`) took 0.81 s on one lane and 0.65 s on 8 without the pool, and 0.51 s
  and 0.33 s with it; on 8 lanes, its `mmap` calls fell from 74,446 to 12,686
  and its context switches from about 100,000 to 43,000. Pooling the three big
  arenas too, rather than keeping them, made the same units take 0.64 s and
  0.38 s: their pages are cleared again for each unit.

  Then the arenas of tables (#1128, [the arenas' guide](arena-memory.md)). A
  lane's first unit still reserved about 100 arenas, each with its `mmap`,
  `mprotect` and page advice, and each unit discarded 68 with `MADV_DONTNEED`,
  each a TLB flush sent to the other lanes. 42 of those were the
  preprocessor's tables and token columns and 11 the parser's columns, now an
  arena per group: a unit discards 24. Lua's link makes 891 `madvise` calls
  rather than 2,389 on one lane, and 1,461 rather than 3,272 on 8, with 481
  `mmap` and 443 `munmap` calls rather than 818 and 786. On the 9700X (CPUs 0
  to 7, medians of 11 runs, main `703ae9f5` interleaved; [the
  record](https://github.com/buster14a/buster-performance/tree/6427b61d795d171e7a053418e565f7320865f62a/docs/measurements/arena-tables-1128-2026-11-03))
  it takes 74.7 ms where main took 75.9 (1 lane), 46.1 against 46.7 (2), 33.1
  against 34.6 (4) and 30.4 against 33.7 (8): 1.6, 1.3, 4.4 and 9.6% less,
  with 55.7 ms of system time on 8 lanes rather than 67.0. Synth's link took
  1,572 ms against 1,597 on one lane and 442.5 against 453.1 on 8; `-c` of the
  frozen `sqlite3.c` on CPU 2 took 110.7 ms against 111.2. Keeping a pooled
  arena's pages and clearing what its next pushes take, rather than discarding
  them, gained nothing up to 1 MB of them and lost 22 to 25% at 4 MB: the
  tables pushed whole but touched sparsely make every pushed byte one to clear.
  `benchmark-check-units`, which the 9700X job runs, reports each lane count's
  system time against one lane's: on 8 lanes, synth's shape still takes 3.6
  times one lane's system time for 3.1 times its speed.
  #1203's review found that this first layout, which rounded each table's
  huge pages out to a whole huge page, mapped more address space than the
  tables' own arenas: under a 512 MiB `RLIMIT_AS` a one-line unit's replay
  for its expansion history no longer fitted. A table now ends at its own
  end, and a group maps no more than its tables' own arenas did. Against main
  `a4a2182a` ([the record's review
  section](https://github.com/buster14a/buster-performance/tree/af6767ef8bfc3f1927f4d75d689eabf1f1525324/docs/measurements/arena-tables-1128-2026-11-03/review)),
  Lua's link took 75.0 ms against 76.6 on one lane and 30.2 against 33.3 on 8,
  synth's 428.0 against 449.4 on 8 lanes, and `sqlite3.c`'s `-c` 110.1
  against 109.7 on nearly the same instructions (21,368 more of 1,184
  million).
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
  calling thread's release after the lanes now takes 0.6 to 0.7 ms. It does so
  before it helps the other units' functions (#777), while they still compile:
  a lane that compiled a small unit beside a large one freed its memory only
  after the large one, when the command waited for it. The lane that stops
  taking units last has no other unit's work to overlap its release with: in a
  command without a link whose process exits after the lanes (main's, which
  releases no memory: `run_check_command_mode`), the exit frees that lane's
  memory with the rest of the address space, as it frees a command of one
  file's. Before a link it releases it as the others do, so that the link's
  peak stays the units': kept through synth's link by one lane, it took the
  peak from 328 MB to 386. The run counts the lanes still taking units
  itself (`CheckUnitsRun`'s `taking`), apart from the board's count, which
  gates the help: the board's allocation may fail on several lanes, and each
  lane read none still taking units from a missing board, so each one took
  itself for the last and left its memory to the exit (#1165's review).
- **It holds its units to the memory** (#1109): each unit, once preprocessed,
  takes an estimate of its peak from what the lanes may hold at once, and
  waits for another lane to give memory back when it doesn't fit (below).

### The units' memory

[The record](https://github.com/buster14a/buster-performance/tree/a0063f8fcf9c5f09217031719c853e33b73c1ce8/docs/measurements/units-memory-1109-2026-11-02)
has this section's and the next one's measurements, their scripts and their
machines.

A lane took one unit after another whatever their sizes, so the process's
peak was about the sum of the lanes' largest units: each lane's three arenas
keep the pages of its largest unit so far. A `main.c`-sized unit adds about
3.5 GB, and build.sh's sources hold ten of them (`main.c` and the
`main_unit_*.c` files that include it), which a run takes last, their
sources being the smallest. On a 4-core VM of 16 GB, the 41 sources at once
were killed by the cgroup's OOM killer at 14 GB, without a diagnostic
(#1109).

So the units' lanes share a memory budget (`CheckUnitsMemory`): seven
eighths of what the process may still take when the run starts
(`check_units_memory_read`: the least of `MemAvailable`, or the free pages
without it, and, for each cgroup v2 from the process's own up to its
hierarchy's root, `memory.max` less `memory.current`, the files' pages on
its lists counted as free, and nothing where a limit's `memory.current`
can't be read, since what the cgroup uses is then unknown and the lanes
take turns; a cgroup v1 limit isn't read, so under one the machine's memory
is the bound), the eighth left for what the estimates
don't count (the links the lanes keep, the lanes' thread scratch, the
lanes that take only functions' parts, the link). Each process budgets
alone: several compilers at once (`make -j`) still add up. A unit takes its
estimate once preprocessed, before its parse (`check_units_admit`, from
`check_driver_unit`), when its tokens are known: the preprocessor's own
memory is a small part of a unit's peak (208 MB of `main.c`'s 3.5 GB),
and its source file's size says nothing of what it includes. A lane holds
its largest unit's estimate since it last gave memory back, as its arenas
hold that unit's pages; a unit that doesn't fit beside what the other lanes
hold makes its lane give back what it holds (its three arenas' pages past
their first huge page boundary, `arena_discard`) and sleep on a word that
every lane advances when it gives memory back, until it fits or no other
lane holds any: a unit alone always runs, however large. A lane gives its
memory back at each unit's end while another lane waits, and once it has
no unit left to take, with its release, its pages freed, before it helps
the other lanes' emissions on the board (#777); the lane that stops taking
units last in a command that leaves its memory to the exit returns its
share alone, since no lane takes memory after it. Which lane that is, the
run's own count says (`CheckUnitsRun`'s `taking`, above), which holds
without the board too: a count read from a missing board would have every
lane take itself for the last and keep its pages while the others still
take memory. The help waits for the lanes still taking units, one of which
may wait for that memory, and a lane that kept it there deadlocked with
such a unit (a 16 MiB unit of comments beside a tiny one, by two lanes
with one unit's memory, found in review). So a lane holds memory only
while it takes units, and waits for nothing but its own units' work then.
What a unit writes doesn't depend on when it ran, so the outputs are the
same as without the budget. `test-check-units` runs its programs' units
with `--units-memory=1`, which serializes every unit's parse and later
stages, into each unit's object alone; reads the budget from files of its
own for each branch of `check_units_memory_read`; requires one of two
equal units by two lanes to wait for the other's memory; and compiles that
slow unit beside the tiny one under two budgets and, under a byte, without
the board (`BUSTER_TEST_NO_UNITS_BOARD`, a test hook that stands for its
allocation failing), each object its own alone and the report saying that
one lane alone kept its pages, where the deadlock would hang until the
children's alarm.

The estimate (`check_units_estimate`) is 600 bytes a preprocessed token and
48 MB a unit. On the laptop, `-c --lanes 1` of each of build.sh's 41
sources, of `sqlite3.c`, of Lua 5.4.8's 34 files and of an empty file and
one of 45 headers (78 units, whose peaks huge pages round up) added from
16.9 MB (the empty file) to 3,899 MB (`main_unit_frontend.c`, 8,087,159
tokens) to the process's resident set. The units of 300,000 tokens or more
took 446 to 644 bytes a token (`main.c` 534, `sqlite3.c` 460), and the
estimate is 1.02 to 1.46 times their peaks (1.14 for `main.c`, 1.20 for
`main_unit_frontend.c`), and 1.17 to 2.85 times the smaller units'.

On the 9700X (64 GB), build.sh's 42 sources at once with `-c` on its 8
lanes (`-march=native` and build.sh's language flags; the process's peak
resident set; each run its own hold of the lock), `08cf75ce` against its
main parent `a4a2182a`, five interleaved rounds by default and two with a
budget:

| run | wall | peak | units that waited |
|---|---|---|---|
| main (`a4a2182a`), median of 5 | 6.58 s | 25.3 GiB | |
| this budget, by default, median of 5 | 6.58 s | 25.4 GiB | 0 |
| `--units-memory`, 12 GiB | 10.95, 10.97 s | 7.7, 7.6 GiB | 9, 9 |
| `--units-memory`, 6 GiB | 17.63, 17.66 s | 4.9, 4.9 GiB | 26, 24 |

By default no unit waited, and the run took main's time and memory (main's
peaks 24.6 to 25.4 GiB, these 25.1 to 25.5): its 25 GiB peak would not fit
the 16 GB VM. Under a 16 GB VM's budget (12 GiB) the `main.c`-sized units
(4.2 GB each by estimate, 4.9 GB for `main_unit_frontend.c`) ran at most
three at a time beside the small ones, and the run took 67% longer and
stayed well under the budget; under 6 GiB they ran one at a time, in 2.7
times main's time. #1109's own command, the sources linked into one
executable (`-o selfhost ... -lm -lpthread`), measured at `a5f0fbc2`,
peaked at 27.7 and 28.1 GiB by default and at 10.99 and 10.98 GiB under
12 GiB, against 10.86 and 10.92 GiB for `-c` under it: the links the lanes
keep add 60 to 130 MB, inside the budget's spare eighth, and the
self-hosted compiler ran. At `a4a2182a` that link fails with the budget and
without it alike: `lane.c` reads `__bss_start` since #652, which buster's
linker defines only for static executables.

### Huge pages on a virtual machine

On #1109's VM, a huge page's first touch took about 1 ms of system time
(the guest clears all 2 MB, and the host backs them, whatever the arena
uses of them; `compact_stall` stayed 0). With transparent huge pages off
for the process (`PR_SET_THP_DISABLE` before `exec`), the checking of
`main.c` ran 2 to 4 times as fast (its `check-input` 14.2 and 8.3 s against 3.3 and 4.3 s), and
Lua's `lvm.c` with huge pages took about 30% longer at the median and up to
7 times as long at the tail. On the 9700X it goes the other way for large
units (`282c4494`, `--lanes 1` on CPU 2, the medians of interleaved runs,
base pages by `BUSTER_HUGE_PAGES=0`; the process's peak resident set):

| unit | huge pages | base pages | peak, huge | peak, base |
|---|---|---|---|---|
| `lvm.c`, `-c` (10 runs each) | 12.6 ms | 12.8 ms | 82 MB | 36 MB |
| `sqlite3.c`, `-c` (6) | 117.4 ms | 144.6 ms | 315 MB | 248 MB |
| `main.c`, `-fsyntax-only` (3) | 1,710 ms | 2,106 ms | 3,484 MB | 3,251 MB |

So `main_driver` decides the process's pages before any arena is reserved
(`arena_page_policy`, `lane.c`): on a virtual machine, by CPUID's
hypervisor bit, transparent huge pages are off for the whole process
(`PR_SET_THP_DISABLE`, which `MADV_HUGEPAGE` doesn't override, and which the
processes it starts inherit); elsewhere the process keeps what it was
started with. `BUSTER_HUGE_PAGES=0` turns them off anywhere, and `=1`
leaves them as the system has them, even on a VM whose huge pages are
backed ahead and cheap. `--time-stages` reports which (`huge-pages`).
GitHub's hosted runners are virtual machines, so the hosted correctness
jobs run with base pages.

### Huge pages on a busy host (#672)

Those measurements ran one compile at a time. With the other cores busy, the
observations go the other way. [The
record](https://github.com/buster14a/buster-performance/tree/bc07aad4fd01b54bf7a00c332e29b21a939551a0/docs/measurements/page-policy-672-2026-11-06)
is exploratory: each script held the 9700X's lock around a series of runs, the
rounds ran in a fixed order, unpaired, and their processes weren't checked one
by one. Its medians are observations, not established differences.
Independent `-c` compiles of the frozen `sqlite3.c`, `--lanes 1`, one process
pinned to each physical core, at main `ec5f0f6b`, 5 rounds:

| compiles at once | huge pages | base pages (`BUSTER_HUGE_PAGES=0`) |
| ---: | ---: | ---: |
| 1 | 67.0 MB/s | 56.4 MB/s |
| 2 | 117.8 MB/s | 104.8 MB/s |
| 4 | 184.0 MB/s | 184.4 MB/s |
| 8 | 233.2 MB/s | 269.7 MB/s |

Over one round of 8, `perf stat -a` counted 15% fewer user cycles and 6%
fewer kernel cycles for base pages. These counts are the whole host's. One
compile alone faults 11 times as often in base pages. Two explanations fit,
and nothing measured separates or establishes them:
- **Clearing.** A huge page's first touch clears all 2 MB at once, of which
  the arena may use little. One compile alone peaks at 294 MB in huge pages
  and at 225 MB in base pages.
- **Locality.** The other cores' traffic can evict what was cleared before
  the arena writes it. A base page is cleared as it is first written.

So where the host is busy when the process starts (`arena_host_busy`), the
process's arenas take base pages (`arena_pages_busy`). The host is busy when
its tasks runnable then (`/proc/loadavg`), this process's among them, are two
or more and at least a quarter of its CPUs online. That is four of the
9700X's 15, where the two modes' observed medians were closest.
- **Mechanism.** Each arena's mapping is advised `MADV_NOHUGEPAGE`, and no
  commit advises it huge. Unlike the virtual machine's rule, this leaves the
  process's other memory as it was, and the programs it starts: `time-runs`'
  timed programs keep the huge pages its parent gave it (#1561's review).
- **Who decides.** A compile alone, as the 9700X job's `measure-compile` runs
  it, keeps huge pages. A process started with them off keeps them off, and
  `BUSTER_HUGE_PAGES` decides instead where it is set.
- **Tests.** `BUSTER_TEST_HOST_BUSY=1` or `0` decides the busy test for them.
  `--time-stages` reports `busy` for this case.

An experiment build of this policy was measured against main, 7 rounds in a
fixed order (the record's `final/`). It is not this change's final
executable. The record has its worktree's source and that tree's diff to
an earlier head of this change, both captured after the runs. That build
turned huge pages off for the whole process (`PR_SET_THP_DISABLE`) where
this one advises the arenas alone.

| compiles at once | main | the experiment build |
| ---: | ---: | ---: |
| 1 | 67.2 MB/s | 67.4 MB/s |
| 2 | 118.2 MB/s | 118.0 MB/s |
| 4 | 184.9 MB/s | 184.1 MB/s |
| 8 | 234.0 MB/s | 270.3 MB/s |

- **The decision.** In the one round of 8 that recorded it, each process
  reported `busy`.
- **Fixed floors.** A floor of base pages in front of every arena, 8 or
  32 MB, had medians 6.5% to 8.7% above main's alone and 3.9% to 4.6% below
  main's at 8.

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
   definition of 'x'"); a weak definition yields to a strong one, and a common
   one (GNU's `common` attribute or `-fcommon`, `BUSTER_LINK_SYMBOL_COMMON`, #147)
   to a strong one too, while it takes a weak one's place and a smaller common
   one's (by its size, then its alignment: `check_units_merge_replaces`, which
   both merges share), as a linker keeps the largest of a name's commons. The symbol
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
  The poisoned commands and `-c -g` also pass `--id-order 153`, comparing
  changed identifier assignments with the original standalone/reference bytes.
- **Diagnostics:** a unit with a warning, one with an error and a clean one,
  with `-c`, `-fsyntax-only` and into an executable, at every lane count:
  standard error is each unit's alone, in the command line's order; `-c`
  writes the objects of the units without errors only, and the link writes
  nothing.
- **Fatal prefix (#153):** a slow earlier warning, two fatal units and a later
  warning, whose source sizes put the later fatal ahead of the earlier one in
  work order. With a missing nested include and with `-Wfatal-errors`, both
  paths at 1/2/3/8 lanes, changed IDs and poisoned arenas must reproduce only
  the first two units' standalone diagnostic groups, notes and summary, for
  syntax checking, objects and linking. Multi-input `-E` is sequential; its
  text and diagnostics, with and without markers and through both kernels,
  must equal the valid units' standalone bytes concatenated in input order,
  with no macro state leaking between them.
- **Refusals:** a name two units define, a name none defines, `-c -o`, a dump,
  two units of one object's name; and that last one in a child
  whose stack was filled with a mapping's address first, which must exit with
  its message as with a clean stack: a local the driver read before setting
  it would release an arena of that address and unmap the child's code.
- **A few large units** (#777): a unit of 800 functions beside a small one's
  `main`, with `-c` by 8 lanes and `--shared-parts` (a test option: a sharing
  emission pauses after each part it takes itself, and the command reports how
  many parts the other lanes took, how many lanes left their memory to the
  exit and how many units gave back their scratch's pages in whole huge
  pages), each object its own alone, the six lanes without a unit having taken
  parts of the large one's, one lane alone leaving its memory to the exit and
  at most one unit giving back its scratch's pages in whole huge pages (a lane
  counts its unit after taking it, and may wake late, so the unit that reaches
  the checker first may find the other untaken); again without the board
  (`BUSTER_TEST_NO_UNITS_BOARD`, a test hook that stands for its allocation
  failing), with no part shared and still one lane alone leaving its memory;
  with `-c` by one lane, each object its own alone and the large unit, taken
  first, alone giving its pages back in whole huge pages, for the small one;
  and linked, where no lane leaves its memory, by `--lanes` 1, by 8 and by the
  scalar path on 8, the same bytes, a program whose sum is the test's.
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
- **The merge-fallback lease:** a small shared scratch arena forces the actual
  parallel reservation to fail; the serial twin then performs its duplicate-name
  check. A test broker observes scratch on the real release control/reply
  protocol while the caller waits for acknowledgement, before serial writes.
  Moving release after the serial call or leaving it to final cleanup fails this
  observation. A refused acknowledgement also checks sticky failure through the
  caller's idempotent final cleanup. The control does not change production
  dispatch or add a callback.
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
