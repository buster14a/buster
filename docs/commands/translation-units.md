# translation-units

Run commands and resolve source paths from the repository root. Follow the
[shared agent rules](../../AGENTS.md), including the 9700X lock and `cores`
requirements. [Command index](README.md).

## Translation units

`check_driver_units` (`c_checker_units.c`) is #152's driver of several source
files: `./build/compiler [options] a.c b.c ...`. The units are a lane group's
items, taken costliest first by their sources' sizes; each runs on one lane
from preprocessing to its code through `check_driver_unit`, the pipeline a
command of one file runs, its functions on that lane and on the lanes that have
no unit left to take (#777: the lanes are a board, `CheckEmitBoard`, on which a
unit's emission that starts while a lane takes no units shares its parts, cut as
function lanes cut them, with the lanes that take none,
`check_program_emit_help`; its outputs one lane's). Each lane keeps an
intern table and a file cache that its units share; no lane shares anything a
unit writes (#776 measured one cache and table for all the lanes, filled by
#150's walk before the units and copied by each lane, and found it slower:
the guide's "One cache for all the lanes, measured"). Each unit's diagnostics
go to a stream of its own, printed in the command line's order after every
unit has run, through the first unit with a fatal (including its notes and
summary). Later buffers are released without printing. A lane keeps its three big
arenas from one unit to the next, emptied; it pools the other arenas its units
release (`arena_pool`, `lane.c`: their pages freed with `MADV_DONTNEED`, and
the mappings and their writable prefixes kept for the next reservations they
hold, #1128; the preprocessor's tables and token columns and the parser's
columns are arenas of tables, each group one mapping, `arena_reserve_tables`);
and the arenas of a
unit of less than 1 MB of source start with 16 MB of base pages
(`arena_small_floor`): fresh arenas' huge pages and the address space's lock,
which every reservation took for writing, were what limited the lanes.
`--poison-arenas`, a test option, overwrites what a unit left in its lane's
three arenas (`arena_poison`), so that a compile that read a byte it didn't
write would show. A unit whose lane has another unit to take gives back its
scratch's pages past the checker's columns in whole huge pages, keeping the
one that holds the peak rather than splitting it into base pages that the
next unit faults in one by one (`check_units_discard_huge`,
`CheckProgramOptions.discard_huge`, `arena_discard_huge`, #777), and
`--shared-parts` reports how many did. The lanes' units are held to the
memory (#1109,
`CheckUnitsMemory`): once preprocessed, a unit takes an estimate of its peak
(`check_units_estimate`: 600 bytes a token and 48 MB) from seven eighths of
what the process may take when the run starts (`MemAvailable`, or the free
pages without it, and each cgroup v2 `memory.max` up its cgroup less what it
uses, nothing where that can't be read, `check_units_memory_read`; not cgroup
v1's) before it parses
(`check_units_admit`); a unit that doesn't fit beside what the other lanes hold
waits until a lane gives its memory back (at its unit's end while one waits,
and once it has no unit left, before it helps on the board), and a unit alone
always runs.
`--units-memory=BYTES`, a test option, sets that budget and reports how many
units waited, the most lanes that held memory at once and how many lanes kept
their pages for the exit. Each lane releases
what its units shared, into its pool and then the pool itself, on the lane as
soon as it takes no more units, before it helps the others' functions (#777);
without a link, in a command whose process exits after the lanes (main's,
which releases no memory), the lane that stops taking units last leaves its
memory to the exit and returns its share of the budget alone. The run counts
the lanes still taking units itself, with or without the board, so that this
holds when the board's allocation fails too. The
command's session's lanes (#905) run the units, the merge, `link_check` and the
executable's write (#778), each a round of the command's flow (`CheckUnitsFlow`)
that lane 0 announces and every lane runs, with a worker-budget lease of its
own. A link keeps each
unit's link (`check_units_keep`) and merges them in the command line's order
(`check_units_merge`: a global name once, each unit's locals its own, a name
two units define an error) before `check_driver_executable` binds and writes
it as it does one unit's; on more than one lane the vector path merges them on
the units' lanes into the same link (`check_units_merge_lanes`, #778: rule 4's
exchange of the global names by their hashes' partitions, then the records
copied at places that prefix sums give), whose exact twin `check_units_merge`
is, and checks the bound link on them (`link_check_lanes`); with `-g`, each
unit keeps its DWARF as `-c -g` writes it, and the merge joins them
(`check_units_merge_debug`, #779). `--id-order N` primes each unit's lane-owned
intern table before its real compile; the output must match an unprimed run.
`-c -o`, the dumps and two
units of one object's name are refused.

`./build/compiler test-check-units [--executable PATH] [--clang PATH]
[--seed N] [--programs N] [--keep] [--corpus DIR [--option ARG]...]
[--generate DIR [--units N] [--functions N]]` runs the driver as a process:
generated programs of 2 to 7 units and one of synth's shape (12 units of 30
functions), each of whose executables, by `--lanes` 1, 2, 3 and 8, by default
and by 8 with `--poison-arenas`, and by each path, is the same bytes, runs as
`clang -O0`'s build does, and defines the symbols `cc`'s link of the units'
objects defines, and each of whose units' `-c` objects is the unit's alone
(by `--lanes` 1, 2, 3 and 8, by default, by 8 poisoned, and by 8 with
`--units-memory=1`, whose report must say that at most one lane held memory at
once and one kept its pages); the units' memory (#1109: the budget read from
files of the test's own, without `MemAvailable` the free pages, a page at least
and bounded, and a limit whose use can't be read leaving nothing; two equal
units by two lanes with a byte of memory, one of which must wait unless the
process may run on one CPU, where the command runs one lane; and
a 16 MiB unit of comments beside a tiny one, by two lanes with a byte, with
one unit's memory and with a byte without the board
(`BUSTER_TEST_NO_UNITS_BOARD`, a test hook that stands for its allocation
failing), which deadlocked before the lanes gave memory back before helping,
and where only the last lane may keep its pages);
diagnostics, each unit's alone in the command line's order; refusals, one
of them over a poisoned stack; the merge on lanes against its twin, by
`--lanes` 1, 2 and 8 and the scalar path (several duplicate definitions, whose
first in the units' order is the one reported, and a wide addend beside a unit
without symbols, into the same bytes); a few large units (#777: a unit of 800
functions beside a small one, whose lanes without units take its functions'
parts, `--shared-parts`' report, into objects their own alone and executables
one lane's, and whose `-c` leaves one lane's memory to the exit, with the board
and without it, `BUSTER_TEST_NO_UNITS_BOARD`, every unit giving back its
scratch's pages as alone by 8 lanes, and by one the large unit giving them
back in whole huge pages); every reservation of two units' `-c` failed in turn, each run
reported and its memory released (a lane's partial setup among them); the
jobserver's tokens held while the link writes; lanes that don't start; and
`benchmark-check-units` by `--lanes` 1 and 2, and its refusals of counts it
can't take (no lanes, a ninth lane count, runs past 1,000 or 64 bits, a count
with more than its digits), of forwarded options that set the lanes, and of a
command that fails every run;
with `-g`, the executables by `--lanes` 1 and 8, poisoned and by each path the
same bytes, and each unit's `-c -g` object its own alone. With `--corpus DIR`
it compiles DIR's `.c` files at once with `-c`, plainly and poisoned, against
each alone. DIR may be relative to the command's working directory; it is
resolved before compiler children change to their output directories. At least
one file must compile alone: an empty or all-failing corpus is a failed test,
even when the failures match. The default suite checks relative and absolute
paths on a mix of valid and invalid files, and rejects empty, all-invalid,
missing and non-directory corpora. With `--generate DIR` it writes a program
of synth's shape (512 units of 200 functions by default) for measurements,
and tests nothing.
[docs/translation-units.md](../../docs/translation-units.md) has the design and the
measurements. The end-to-end single- and multi-unit contract, with the
mixed-tier and actual command-team receipts, is
[the multicore acceptance guide](../../docs/multicore-acceptance.md);
`tests/driver/multicore.c` runs it in the shared correctness suite, with changed
identifier IDs in both single- and multi-file repeats. `test-check-units` also
checks missing nested includes and `-Wfatal-errors` through the first fatal
unit at 1/2/3/8 lanes, through both kernels with changed IDs and poisoned arenas,
and the sequential multi-input `-E` text and diagnostics against the valid
units' standalone bytes in both marker modes. What
#149's remaining whole-process performance acceptance still needs is there
too.

`test-check-units` also covers `selectany` through
`c_check_selectany_driver_test.c`: Clang's weak COMDAT signatures and grouped
relocations, zero-fill/const/TLS data, equal explicit section names in different
groups, selectany aliases of ordinary and grouped definitions, stronger ordinary
definitions, and a relocation to an undefined symbol
that must disappear with its discarded group. Direct linking uses the same
first-group choice through one lane, the scalar twin, multiple lanes and
poisoned arenas. Single-unit runtime binding, debug output and Tier 1 are covered
too; reversing the units must retain the formerly discarded undefined reference.

`./build/compiler benchmark-check-units [--executable PATH] [--units N]
[--functions N] [--lanes N,N...] [--runs N] [-- OPTION...]` (#1128) times one
command of synth's shape (64 units of 200 functions by default) by `-c`, with
the options after `--` (absolute paths: the processes run in the program's
directory), by each lane count (1, 2, 4 and 8 by default), as processes in
`time-runs`' rotated order, 5 runs of each after a warm-up, every one of which
must compile the units (`time-runs` alone passes a status that stays the same).
Counts are whole decimals from 1: at most 4,096 units, 100,000 functions, eight
lane counts of at most 4,096 lanes, and 1,000 runs. The options after `--` may
not set the lanes (`--lanes` or `--worker-budget`, either spelling): the driver
would take theirs over each command's count.
Then it prints each count's wall time and system time against the first
count's: lanes that serialize on the kernel, as #1128's did on the address
space's lock, raise the system time where a busy machine's wall time may hide
it. The 9700X job runs it by 1, 2, 4 and 8 lanes with the frozen corpus's
headers and predefines, where `tools/ci/changes.c`'s `check-units` says.
