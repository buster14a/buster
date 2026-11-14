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
write would show; and a unit with a file cache of its own makes its files'
token columns unreadable after its run where it would give them back
(`pp_release_file_columns`, #628), and its output's before the backend
(`pp_release_output`), so that a reader left would fault. A unit whose lane has another unit to take gives back its
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
and without it, `BUSTER_TEST_NO_UNITS_BOARD`, at most one unit giving back
its scratch's pages in whole huge pages by 8 lanes, and by one the large
unit alone); every reservation of two units' `-c` failed in turn, each run
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
`time-runs`' order of a Williams design, 5 runs of each after a warm-up, every one of which
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

## Attested declarations (`--attested`, #2531)

A command of several source files skips, in each unit, the file-scope
declarations that its lane's earlier units read silently and purely and that
the unit's own code doesn't reach: steps 1 and 2 of [attested
declarations](../attested-declarations.md) (`c_check_attested.c`, the passes
the census below measures, run in the compile). `--attested=MODE` picks:

- **`on`, the default:** each units lane opens a state of its own with its
  first unit (`check_attested_lane_open`: a memo of 128-bit semantic hashes,
  and each name's state by the lane's intern table's ids, which its units
  share), and `check_driver_unit`'s reader (`check_attested_read`) runs the
  passes before `check_input_run`, which gives the skipped declarations' tokens
  no visit and ignores them (`check_input_skip`, as a lazily deferred body's),
  so that the walk passes them. The tokens stay, lexed and parsed, and keep
  their indices. The reader's intrinsics' pass leaves the skipped
  declarations' operands out too (`CheckInputReader.skips`), and its bindings'
  heads sit uncleared in its own fresh arena (`c_bindings_initialize_zeroed`,
  `arena_untouched`), where a unit of a lane, whose intern table holds every
  unit's names, cleared them all. The lane keeps a cache of its declarations'
  lists by their own hashes (`CheckAttestedListSlot`, the passes' vector path,
  `check_attested_lists_cached`), and of a cached declaration's tree columns
  beside it (`check_attested_tree_columns`): the reader takes a hit's columns
  from it, its parents compared, where `parse_tree_columns` made the unit's,
  and the system tokens from the levels the passes mapped
  (`check_attested_token_levels`). A function definition's semantic hash is
  its own in the lane (it is never attested), so that the passes don't hash its
  body. The expressions' walk gives a skipped declaration's tokens no class
  (`check_input_expression_classes`): a whole file-scope tree of ignored
  tokens, none of whose values reaches a token read.
- **`verify`:** the same passes, and nothing skipped: a hit whose reading here
  isn't silent, pure and listed (a contradiction: an input of a reading that
  the semantic hash doesn't cover) fails the unit with a fatal error naming
  its file and line. Step 1's shadow. The unit's tree columns from the cache are
  also made whole and compared, a difference a contradiction.
- **`--attested-memo=DIRECTORY`** (step 4, opt-in): each lane's memo is kept
  across processes, in a file of the directory for each key: the compiler's
  build (its GNU build ID, read from its own program headers, or its
  executable file's identity) and the command's options but for its inputs,
  outputs, lanes and reports (`check_attested_memo_key`). A lane loads the
  file as it opens (`check_attested_memo_load`, the table sized once for its
  records) and appends the hashes it learned as it closes
  (`check_attested_memo_save`), each under the file's `flock`, so that a
  build's processes share it; a file whose header isn't its key's, torn or
  past its most records is written again with the lane's memo whole. Its
  hashes are portable: a name's word is its spelling's hash
  (`CheckAttested.id_words`) where a lane's own memo takes its id, and a
  function definition's own hash takes the process's nonce. A command of one
  source file runs the passes with it too (`check_driver_one_unit`): its
  state's arenas are base pages throughout and stay mapped to the exit, it
  keeps no caches of lists or trees (`CheckAttested.alone`), and its memo is
  the hashes in an array, read by one call, with a table of their indexes
  (`check_attested_memo_load_alone`), and it reads its unit as its view only
  where it skips a third of the unit's tokens or more. Its diagnostics, the
  preprocessor's and the checker's, go to an in-memory stream of their own,
  and a unit that skipped and failed is read again from its start with nothing
  skipped, so that only the second reading's diagnostics print. A link without
  a build ID (the CI toolchain's lld adds none) is keyed by its executable
  file's identity instead: device, inode, size and modification time.
- **`off`:** no passes and no state. `--scalar` is `off` (its reader is the
  skip's oracle), and so is `on` with `-g`, whose DWARF describes every type
  the unit's table holds (`c_check_debug_values.c`), a skipped declaration's
  too; `verify` runs with `-g`. A command of one source file has no memo and
  runs none.

As the reader ends, while it has its binding events, it tests each reading's
file-scope `DECLARE` and `USE` events and its tags' completions against the
declaration's list (`check_attested_events`, a declaration's list marked by key
as its events start). At the unit's end (`check_attested_after`) the memo
learns the misses read silently (no diagnostic placed at their tokens by output
position, or by location for one published past the last token, as
`-Wunused-function`'s), purely and listed, in a unit without an error. A unit
that skipped is read again from its start with nothing skipped, into its stream
from its start and its arenas emptied (`check_units_compile`), where its first
reading may have depended on what it skipped: an error or the failure of any
stage, the typo corrector, an unlisted event or a contradiction. Nothing reaches
the lowering's runtime functions' names in the compile: a runtime call takes a
declaration's symbol only for an object the selection numbered
(`check_lower_declared_runtime`, `check_program_needed_number`), which only a
reference of the selected code makes, and a reading that spells the name or its
builtin reaches the declaration.

`--time-stages` reports the lanes' counts (`attested declarations: ...`): the
units that skipped, the file-scope declarations and the tokens outside function
definitions skipped, the units read again, the memos' hashes, the
contradictions and the unlisted events. `test-checker --attested` (in
`c_check_attested_test.c`) compiles a fixture's units by each mode: by `-c`
with two units' errors (one an undeclared call whose typo correction suggests a
skipped prototype), on two lanes, by `-fsyntax-only -Wall -Wextra`, with `-g`
and with `--scalar`, each object and the diagnostics `--attested=off`'s bytes;
and `test-check-units`' every object of a command against the unit's alone is
the skip's test on its programs. The design's "Steps 1 and 2" has the 9700X's
results.

## Attested declarations' census (#2531)

`./build/compiler measure-attested [--list N] [--passes N] [--twins] [compile options] file...`
(`c_check_attested.c`) is step 0 of [attested declarations](../attested-declarations.md):
the command of several units, run as `--lanes 1` runs it (the census refuses
its own lanes' options and `--deferred-bodies`), with the design's passes and
each declaration's record beside every unit. No compile changes: every
declaration is read, and the counts say what step 2 would skip.
`check_driver_unit` tests `check_driver_attested`, which only this command
sets, at two places: its reader's call and its unit's end.

- **The passes, before the reader:** the lists (each file-scope declaration's
  range, a subtree whose root is the parser's root's child, and each
  identifier's class, from its role and the groups open around it: a tag or a
  declarator's name declares, a typedef's specifier and any name in an
  expression, an array's bound, an initializer, an attribute or a `typeof`
  looks up, a parameter's and a member's name is neither; in a function's
  body every name looks up, and only a callee's name, or every name in a body
  that spells `extern`, declares), the own hashes (a sum over each
  declaration's tokens of its kind, role, system level and name or literal at
  its place), the chain (each declaration's semantic hash from its own, the
  pragmas before it and the current hash of every name it lists; the memo's
  probe; its declared names' new hashes), the reach and the marks. On the
  vector path the declarations' bounds go 64 tokens at a time, then the own
  hashes eight a register (a function definition's is zero, its body unread),
  then each declaration's list from the lane's cache by its own hash, or made
  a token at a time and kept; `--scalar` runs the lists' walk over the unit,
  their twin, and the own hashes' scalar loop after it. A body's list holds
  each key and class once. `--twins` makes each unit's lists, own hashes and
  literals again by the other path and counts the units whose results differ,
  and makes the reader's tree columns whole too and counts each unit whose
  cached columns differ as a contradiction. Each pass is timed by the thread's user-mode cycles, branch
  misses and L1D read misses (`rdpmc`) and TSC ticks: the first run's, the
  one inside the compile, and with `--passes N` the least of N runs. The
  report gives them a token of the units' tokens.
- **The reader** is `check_input_run` as `-c` calls it, which tests each
  reading's binding events against its declaration's list as it ends
  (`check_attested_events`, the compile's own).
- **The record, at the unit's end:** silent (no diagnostic of the driver's
  collected ones, every stage's, placed at one of the declaration's tokens by
  its output position), pure (no token with a definition, a tentative
  definition, internal linkage, a global register or an alias, and no
  file-scope asm statement), and listed (each file-scope `DECLARE` event, each
  tag it completes and each `USE` event is for a name its list holds). A unit
  with an error, a failed parse or a diagnostic no declaration holds teaches
  the memo nothing; otherwise the memo learns the semantic hashes of the
  declarations read silently, purely and listed. A hit whose reading here
  isn't that is a contradiction: an input of a reading that the hash doesn't
  cover.
- **Names no token spells** count as spelled by a root: the library function
  of a `__builtin_` name a reached declaration spells, the identifiers in the
  strings of a reached declaration's asm and attribute groups, and in a unit
  with a body the runtime functions the lowering looks up by name in every
  such unit (`check_lower_runtime_names` before `CHECK_LOWER_RUNTIME_LIBRARY`),
  whose first declaration gives a runtime call's symbol. What that last rule
  alone reaches is its own outcome, `reached-by-runtime-names`.
- **The report** counts declarations and their tokens by outcome (`skipped`:
  attested and not reached; `reached`; `learned`; a root's; a diagnostic's; an
  impure reason's; an error's unit's), the share of the tokens outside
  function definitions that would be skipped, the binding events against the
  lists (`unlisted`, the gate's zero), the contradictions, the diagnostics and
  the literals, the lists' cache (`lists cached`, `made`, `kept`) and the
  trees' (`trees cached`, `made`, `kept`, `mismatches`, the gate's zero), the
  twins' differences and each pass's counts (on the vector path the lists'
  time holds the own hashes'). `--list N` prints the first N
  declarations of each outcome but `skipped`, and every unlisted name and
  contradiction, by unit, file and line.

`test-checker` (or `test-checker --attested`, alone) runs
`c_check_attested_test.c`: commands of a few units over a header whose lines
say each declaration's outcome by unit (learned in the first; skipped, reached,
impure, a root, a diagnostic, an error's unit's in the next), by `-c`,
`-fsyntax-only` and `--scalar`, with `--twins`; a typedef another unit declares otherwise and
a declaration after `#pragma pack` being other hashes; a system header's
declaration and a user's of one text being two; and the refusals of its own
lanes' options and of a run without a file. Each command must have no unlisted
name and no contradiction. [The design](../attested-declarations.md)'s "Step
0: the census" has the results on musl's and the glibc units' commands.
