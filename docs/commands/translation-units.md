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
unit has run. A lane keeps its three big
arenas from one unit to the next, emptied; it pools the other arenas its units
release (`arena_pool`, `lane.c`: their pages freed with `MADV_DONTNEED` and the
mappings kept for the next reservations of their shapes); and the arenas of a
unit of less than 1 MB of source start with 16 MB of base pages
(`arena_small_floor`): fresh arenas' huge pages and the address space's lock,
which every reservation took for writing, were what limited the lanes.
`--poison-arenas`, a test option, overwrites what a unit left in its lane's
three arenas (`arena_poison`), so that a compile that read a byte it didn't
write would show. Each lane releases what its units shared, into its pool and
then the pool itself, on the lane once it takes no more units, and the
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
(`check_units_merge_debug`, #779). `-c -o`, the dumps, `--id-order` and two
units of one object's name are refused.

`./build/compiler test-check-units [--executable PATH] [--clang PATH]
[--seed N] [--programs N] [--keep] [--corpus DIR [--option ARG]...]
[--generate DIR [--units N] [--functions N]]` runs the driver as a process:
generated programs of 2 to 7 units and one of synth's shape (12 units of 30
functions), each of whose executables, by `--lanes` 1, 2, 3 and 8, by default
and by 8 with `--poison-arenas`, and by each path, is the same bytes, runs as
`clang -O0`'s build does, and defines the symbols `cc`'s link of the units'
objects defines, and each of whose units' `-c` objects is the unit's alone;
diagnostics, each unit's alone in the command line's order; refusals, one
of them over a poisoned stack; the merge on lanes against its twin, by
`--lanes` 1, 2 and 8 and the scalar path (several duplicate definitions, whose
first in the units' order is the one reported, and a wide addend beside a unit
without symbols, into the same bytes); a few large units (#777: a unit of 800
functions beside a small one, whose lanes without units take its functions'
parts, `--shared-parts`' report, into objects their own alone and executables
one lane's); every reservation of two units' `-c` failed in turn, each run
reported and its memory released (a lane's partial setup among them); the
jobserver's tokens held while the link writes; and lanes that don't start;
with `-g`, the executables by `--lanes` 1 and 8, poisoned and by each path the
same bytes, and each unit's `-c -g` object its own alone. With `--corpus DIR`
it compiles DIR's `.c` files at once with `-c`, plainly and poisoned, against
each alone, and with `--generate DIR` it writes a program of synth's shape
(512 units of 200 functions by default) for measurements, and tests nothing.
[docs/translation-units.md](../../docs/translation-units.md) has the design and the
measurements. The end-to-end single- and multi-unit contract, with the
mixed-tier and actual command-team receipts, is
[the multicore acceptance guide](../../docs/multicore-acceptance.md);
`tests/driver/multicore.c` runs it in the shared correctness suite. What
#149's remaining whole-process performance acceptance still needs is there
too.
