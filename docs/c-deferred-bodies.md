# Deferred function bodies

`c_check_deferred.c` is [#113](https://github.com/buster14a/buster/issues/113)'s
deferred reader. `check_input_deferred` checks a translation unit as
`check_input` does, with the same `CheckInput` and `TypeTable` out, but reads
each file-scope function body apart from the top level, out of order and on
any number of lanes. It then merges the bodies into the reader state that
source order's walk leaves, and `check_input_run`'s later phases run on that
state as they run after a walk in source order. The result is source order's,
column for column, diagnostics among them: the tests below compare every
column. A unit the reader can't merge yet is checked by `check_input` from
nothing, and the reader says why (`CheckDeferred.unsupported` and `.reason`).

## The reader

1. **The top level.** `check_input_run` walks the unit with every file-scope
   body deferred (the `deferred_tokens` mask), on a table of the reader's own
   that records its changes (`TypeChanges`, below). Where the walk skips a
   body, `check_deferred_cut` records the reader's counts there: types and
   tags, binding events and active declarations, and each table the walk
   appends to (strings, statement-expression regions, VLAs, asm statements,
   varargs, cached constants). The activations at a body's opening (its
   function's own declaration) are the top level's, as in source order.
2. **The file scope's history.** `c_environment_build` (#319's index,
   [the environment guide](c-environments.md)) indexes the top level's
   file-scope declarations by name and activation position.
3. **The lanes.** The reader runs one lane, and one more per
   `BUSTER_CHECK_DEFERRED_LANE_TOKENS` (12,288) of the bodies' tokens beyond
   twice the largest body's, with no more lanes than it is given or than
   bodies: a lane costs its copy of the reader and its wake-up, which its share
   of the walks must pay for, and a lane woken for the bodies reads them at
   about half the first lane's speed, so the lane that takes the largest body
   may take twice as long as the first would ("How many lanes", below).
   `CheckDeferred.every_lane` runs every lane the bodies can take, as the tests
   and `test-checker-corpus` do. `lane_parts_by_cost` cuts the bodies into parts by token count and each
   lane takes parts with `lane_take`, costliest first. A lane copies the top
   level's reader once, when it takes its first part
   (`check_deferred_lane_prepare`), so that a lane that takes none copies
   nothing (#432's review): its
   binding stacks (the top level's events, then room for the largest body),
   the per-name and per-tag arrays the walk writes, the scope stacks and
   reports, the appended tables, and the table (`type_copy`, with room for the
   largest body's types). The copies are columns with room for the largest
   body that a lane mostly never touches, so its arenas keep their first 64 MB
   in base pages: the kernel clears a huge page whole the first time any of it
   is touched. The table's index has room for the top level's types and 4,096
   more, and grows past them as any index does.
4. **A body** (`check_deferred_body`). The lane sets its reader to what source
   order's is at the body's opening, the position checks:
   - each name the body and its parameters spell, once (a stamp per name),
     gets its ordinary and tag heads from the history at the opening, 16 keys
     at a time (`c_environment_lookup_batch`), and its label head none. A
     declaration the history gives that a restore ended at or before the
     opening gives way to the latest of the name's earlier ones still active
     (`check_deferred_visible`). Only a declarator that fails ends one, and its
     unit is source order's: a tag first named in a file-scope prototype
     (`void g(struct S *p);`) is of the prototype's scope, a level deeper than
     the file scope the history holds (#438);
   - the table's types made after the opening are hidden
     (`TypeTable.hidden_begin`), and the top level's completions and
     transparency after it withdrawn (`type_tag_withdraw`). The withdrawals
     are in position order; a binary search finds the first after the opening,
     and the lane moves from the withdrawals its last body had to these by the
     difference (`check_deferred_withdraw_to`), restoring what is now before
     the opening (`type_tag_restore`). A tag has one completion at most and
     one transparency at most, after it, and the two commute, so the moves
     compose; a restored tag's transparency comes out again where it is still
     after the opening;
   - the predefined types (`__builtin_va_list` and the others) the top level
     made after the opening are unmade.

   Then `check_input_resolve_range` walks the body, from its opening to the
   token after its closing, as a lane (`CheckInputReader.lane`: the opening's
   activations are the top level's, and only scopes end after the closing).
   Its result is kept from the lane's copies: its type changes (from the
   lane's one record, which each body takes back, packed), its binding events,
   its appended entries, the completion positions of the tags it made, the top
   level's VLAs it changed (its parameters') and the predefined types it made.
   Then the lane takes its names' heads and its view back.
5. **The merge** (`check_deferred_merge`), in source order, on the caller's
   thread:
   - `type_replay` makes the top level's changes and each body's again in the
     caller's table, merged by key (position, then order at the position), the
     top level's first at a position both have. Each change's types are mapped
     to the replay's, and `last_error` is the last write's by key;
   - each appended table is laid out with each body's entries at its cut, where
     source order appends them, and every id into it mapped;
   - each body's events go at its cut, their handles mapped, a restore's mark
     past the top level's active declarations at the cut;
   - every column the walk wrote with a `TypeId` or a table id is mapped
     through its owner's part: a body's tokens and its parameters' through the
     body's, the rest through the top level's. The tags' completion positions,
     linked types, predefined types (the earliest maker's by position),
     `__func__` strings and intrinsic declarations follow.

Each body's walk writes the shared per-token columns only at its own tokens
and its parameters', which no other walk writes; everything else it writes is
its lane's copy. ThreadSanitizer found one place that read another body's
cells: `check_input_vla_parameters` read the body column of every VLA's
declaration. It now reads only the body's own parameters', the only ones that
can match. With that, ThreadSanitizer reports nothing on the fixtures,
c-testsuite, SQLite and Lua.

## The type table's changes

`c_types.c` records a table's changes when `TypeTable.changes` is set
(`type_changes_initialize`, `type_changes_at`): each derived type, tag,
definition, layout, enum completion and transparency, keyed by the caller's
position and an order there, with its parameters or members; and every write
of `last_error` with its key. `type_copy`, `type_view_mark` and
`type_view_reset` give a lane a table it can take back to a mark, with the
types made since out of its index. `type_replay` takes several records (the
top level's, then the bodies' in source order) and makes them again into a
fresh table; a body's record maps the top level's ids through the top level's
part. A tag predefined in two records (each record's first construction of a
predefined type) is kept once, the first by key. `test-types` checks it
(`c_types_deferred_test.c`) on 600 random units of type operations, 15,304
bodies in all, read on 1, 2, 3 and 8 lanes in random orders: the replayed
table must be the one source order builds, column by column with its index,
and every operation's result and every question a body asks of the types it
sees (complete, size, transparent) source order's.

## What goes to source order

The reader merges what source order makes at a body's position. A unit goes to
source order, said with the reason:

- the top level reports an error, or its walk stops;
- the unit has overloadable functions, whose sets later declarations join;
- a body declares something with linkage (`extern`, a function at block
  scope): the file-scope entity's type, which later declarations see;
- a body makes a union transparent, which applies to what follows it;
- a body uses an intrinsic, whose tables the merge doesn't take yet;
- a body completes a tag the top level made: its definition's parameter-list
  tag, which the body declares again (#436), whose completion everything after
  the body sees in source order and the merge doesn't carry yet;
- a body's walk reports an error (a later phase's error is merged like any
  other: the phases run on the merged state);
- a table runs out of room.

The fallback checks the unit from nothing, with the caller's table taken back
to how it came: a merge that fails after its replay has filled it.

The table's transparency is withdrawn as its completions are, so that a
lane's table is source order's at the opening, as `test-types` checks. The
walk reads transparency by position (`transparent_unions`), so no checker
test can tell the reader's withdrawal of it apart now: a mutant without it
passes every checker test.

## Tests

- `./build/compiler test-checker-deferred [--keep]`: 50 fixtures, 44 of the
  issue's cases and 6 of the lanes' rule, each written to a file and checked as
  `test-checker-corpus` checks one: on the scalar path, the vector path, and the
  vector path with every body read apart, on 1, 2, 3 and 8 lanes through the
  stacks, the batch and the direct binding resolution, the issue's cases on
  every lane their bodies can take (`CheckDeferred.every_lane`). The
  deferred result must be the vector path's column for column, and each fixture
  says whether the reader merges it. A merged unit's bodies must run on the
  rule's lanes (`CheckDeferred.active`), and its reader be copied by one of them
  at least and by no more (`CheckDeferred.copies`). Each of the issue's cases
  runs again on 8 lanes by the reader's own rule, which reads most of them on
  one. The rule's fixtures are functions of empty statements whose bodies have
  known tokens, read by the rule alone, whose bodies, tokens, largest body and
  lanes must be the ones they were made with: on each side of a second lane
  (`BUSTER_CHECK_DEFERRED_LANE_TOKENS` tokens beyond twice the largest body's,
  and one fewer) and of an eighth (seven times as many, and one fewer), and a
  largest body of four times as many beside four bodies of as many, a token
  fewer, which take one lane, and beside five, which take two. Mutants of the
  rule fail: main's cap of as many lanes as bodies and a lane too many 54
  checks each, the largest body counted once 33, a ceiling's division 17, and
  `every_lane` not read 186. Each of the issue's cases that merges runs again
  on 8 lanes with the first lane taking every part (`CheckDeferred.hold_lanes`), when
  no other lane may copy the reader; on 1 and 8 lanes with its merge failing
  right after the replay (`CheckDeferred.fail_after_replay`), which must give
  source order's result; and with the allocation of the reader's state failing
  (`check_corpus_deferred_fail_state`), which must count as a resource failure
  (`test-checker-corpus`'s `resources`), not as a result that differs. The 400
  bodies are read again with the reader timing what its lanes cost
  (`CheckDeferred.timing`), on one lane and on every one of 8: one lane must
  wait for no other, and `lane_run`'s dispatch and the wake-up must fall within
  the lanes' span. A copy whose wake-up counts from `lane_run`'s call, and one
  whose dispatch is the lanes' span, each fail it. Then
  `benchmark-checker-deferred`'s `--cpus` parser, into room for 64 CPUs that
  ends at a `PROT_NONE` page: lists of 1 to 64 are read whole, one of 65 is
  refused without a write past the room, and so are malformed lists.
  They are: the issue's three (a later declaration, a later completion, an
  enumerator from an initializer); typedefs shadowed and redeclared after a
  body; tags completed, redeclared and shadowed around bodies, and completed
  between bodies where the walk itself needs them complete or not (a local
  object, local records, an enum, a typedef); block-scope declarations with
  linkage; strings and `__func__`, statement expressions, VLAs, varargs, a
  predefined type first made in a body, asm statements, cached constants and
  `_Generic`, compound literals and statics; recursion and forward calls,
  labels; unions made transparent in a body and at file scope between bodies;
  tags first named in a prototype's parameter, a sizeof in an array's bound
  and a definition's parameter, and one declared before its prototype; a
  definition's parameter-list tag used in its body, which merges, and
  completed there, which goes to source order;
  errors in a body, at the top level, in the syntax and in a later phase; and
  400 bodies that call each other. Mutants that skip a lane's withdrawals
  when it moves back, or its restores when it moves on, fail 12 and 9 checks;
  one without the restores' check of the history's declarations fails the
  five tags' fixtures, and one without the table's reset 19 fixtures.
- `./build/compiler test-checker-corpus --deferred N [--deferred-bindings
  stacks|batch|direct] ...` adds the deferred reader on N lanes as a third path
  to each file's comparison, on every lane the bodies can take up to N
  (`CheckDeferred.every_lane`: how many lanes read the bodies doesn't change
  the merge's result, which the comparison checks on as many as it can), which
  must give the vector path's columns; a
  difference counts as a path difference and prints the first differing
  column. The summary gives the units merged and left to source order, their
  bodies, the differences, and the units left to source order by reason.

The 9700X job runs `test-checker-deferred`, c-testsuite's programs with
`--deferred 8`, and the frozen SQLite source's check with `--deferred 8`.

On the laptop (Zen 4), with `--deferred 8` unless said, each against clang:

| Corpus | Units merged | Left to source order | Bodies read apart | Differences |
| --- | --- | --- | --- | --- |
| c-testsuite and `sqlite3.c` | 220 | 1: a declaration with linkage in a body | 3,057 | 0 |
| the same on 1 lane, batch resolution | 220 | 1, the same | 3,057 | 0 |
| Lua 5.4.8 (`-std=gnu99 -O2`) | 34 | 0 | 1,303 | 0 |
| musl 1.2.5 `src` (1,524 files) | 1,486 | 38: 37 errors in a body, 1 that doesn't parse | 21,966 | 0 |
| Zig 0.14's `lib/libc` (2,315 files, 970 that don't preprocess alone) | 1,004 | 341: 256 that don't parse, 43 errors in a body, 26 at the top level, 16 intrinsics | 3,139 | 0 |

ASan with UBSan and ThreadSanitizer builds report nothing on the fixtures,
c-testsuite, SQLite and Lua.

## Measurements

`./build/compiler benchmark-checker-deferred [--passes N] [--lanes N] [--cpus
LIST] [--every-lane] [preprocessor options] file...` preprocesses each file
once, then per pass parses it and checks it by source order's `check_input` and
by the deferred reader on one lane and on up to `--lanes` (8), in their six
orders, one a pass in turn: in any six passes in a row, each reading comes
first, after the parse, twice, and after each other reading twice, so
`--passes` is a multiple of six, 24 by default. A deferred reading ran up to 3%
faster after the deferred reader's own than after source order's. The three
orders that rotated before put the reading on `--lanes` after the one on one
lane in two passes of three, and the six in 21 passes, as #448's review
counted, put it there 8 times and after source order's 6. It reports the
bodies, their tokens and the largest body's, and medians
in nanoseconds per token: the parse; source order's reading; and each deferred
reading's whole, its top level, the history's index, the bodies' walks and
their position checks (summed over the lanes), the lanes' span, the merge and
the rest (the phases after the merge); the position checks per body too, the
lanes the bodies ran on (`active`: as many as the rule gives them, or with
`--every-lane` every lane they can take) and those that copied the reader
(`copies`); and what a lane costs, in nanoseconds: a copy of the reader
(`copy`); `lane_run`'s dispatch, from its call to the first lane's arrival at
the partition (`dispatch`: the first lane's pinning and the others' wake
calls); and the others' wake-up, from the first lane's arrival there to the
last one's (`wake`, none on one lane), which is the first lane's wait at the
partition's sync. Before #448's review, `wake` was the first lane's whole time
in `lane_parts_by_cost`, its share of the boundaries and its order of the parts
with its wait, and left the dispatch out. `--cpus` pins the command to the
first CPU, where the parse, source order and the top level run, and its lanes
to the list. The measurements before "How many lanes" were made with the three
rotating orders.

On the 9700X, on the frozen `sqlite3.c` (706,237 tokens, 2,608 bodies) with
`--cpus 2,0,1,3,4,5,6,7`, the medians of 9 passes, ranges over three processes
([the record](https://github.com/buster14a/buster-performance/tree/34d55c24d9d74f452c51c9919bca6d48c0316220/docs/measurements/deferred-bodies-113-2026-10-25)
has the method and the logs), in nanoseconds per token:

| | One lane | Eight lanes |
| --- | ---: | ---: |
| the parse | 13.47 to 13.53 | |
| source order's reader | 105.13 to 105.50 | |
| the deferred reader | 122.10 to 123.92 | 107.30 to 108.00 |
| its top level | 40.80 to 41.15 | 41.03 to 41.19 |
| the bodies' walks (summed over the lanes) | 17.04 to 17.50 | 25.64 to 25.67 |
| their position checks (summed) | 3.240 to 3.264 | 4.796 to 4.836 |
| the lanes' span | 21.86 to 22.32 | 6.22 to 6.34 |
| the merge | 6.81 to 6.89 | 7.10 to 7.20 |
| the phases after it | 52.44 to 52.82 | 53.05 to 53.29 |

The position checks cost 3.25 ns/token on one lane, 877 to 884 ns a body: 24%
of the parse and 3.1% of source order's reading. On eight lanes the bodies take
6.3 ns/token of span, and the reader comes to 2 to 3% above source order: what
is left is sequential, the top level, the merge and the phases after it, 101 of
its 108 ns/token. Before the lanes' arenas took base pages and their indexes room
for the top level's types alone, eight lanes took 136 ns/token, and their walks
64 in all: each lane's copies are columns with room for the largest body, and a
huge page is cleared whole the first time any of it is touched.

On small units, a lane's copy of the reader and its wake-up are most of what
eight lanes cost. Before #432's review, every lane copied the reader before the
partition, so a unit of one body made eight copies; now the reader runs no more
lanes than bodies, and a lane copies the reader when it takes its first part.
On six of c-testsuite's programs on the 9700X ([the
record](https://github.com/buster14a/buster-performance/tree/b7e0d9fb790b4b4adccbfb483ee91812b5cee1c1/docs/measurements/deferred-lanes-432-2026-10-25)),
eight lanes' reading of a unit of one body costs what one lane's does: 66% less
than before on `00001.c` (9 tokens) and 28% less on `00169.c` (3,054 tokens),
and 8 to 23% less on units of 2 and 5 bodies. `00182.c` (11 bodies) and
`00204.c` (108), whose bodies keep every lane busy, don't change in a process of
their own, nor `sqlite3.c`. Where the bodies are small, eight lanes still cost
more than one (`00213.c`'s five bodies: 365 against 225 ns/token): a lane's copy
and wake-up outweigh what a small body gives it.

### How many lanes

A lane costs its copy of the top level's reader and its wake-up, which only its
share of the bodies' walks can pay for. On the 9700X, with `--cpus
2,0,1,3,4,5,6,7` and the frozen corpus's options, each unit and lane count in a
process of its own, the reader on 2 to 8 lanes against one lane, on 35 units:
six of c-testsuite's programs, the frozen `sqlite3.c`, and 28 synthetic units
of 2 to 70 bodies after `stdio.h`, `stdlib.h` and `string.h`, after thirty of
the C library's headers or after none ([the record](https://github.com/buster14a/buster-performance/tree/8f57e7f6bf92fd7a9420079fc09c73a4a8985264/docs/measurements/deferred-lane-rule-113-2026-10-25)):

- **A lane's copy** takes 30 to 70 µs when one lane makes it after the three
  headers (95 to 110 µs after thirty, 350 µs on `sqlite3.c`), 50 to 120 µs on
  average when two lanes copy at once, and 175 to 295 µs when eight do (360 to
  405 µs, and 845 µs).
- **The wake-up:** `lane_run`'s dispatch, from its call to the first lane's
  arrival at the partition, takes 0.6 to 7.5 µs on one lane, where it pins the
  first lane (more after a larger unit), and 1.1 to 10.7 µs on two lanes and
  3.0 to 15.1 µs on eight, where it wakes the others too. The first lane then
  waits at the partition for the last of the others: 15 to 24 µs on two lanes
  (2 µs on the two units whose readings take under a millisecond, in five
  rounds of their six) and 13 to 21 µs on eight. `lane_run` wakes the others
  before the first lane enters, so on eight lanes their wake-up runs partly
  during the dispatch: the last lane arrives 16 to 36 µs after `lane_run`'s
  call on eight lanes, and 18 to 35 µs on two (3 µs on those two units). Before
  #448's review, the wait's figure was the first lane's whole time in
  `lane_parts_by_cost`, which counts the partition's own work too: 0.1 to 0.5
  µs on one lane, which waits for no one, and on eight lanes within 1 µs of the
  wait on the synthetic units but 6.7 µs above it on `sqlite3.c`'s 2,608
  bodies (the record's log 9: six units, three rounds, each run a process).
- **A woken lane's walk** of a body token takes 1.8 to 2 times the first
  lane's (15 ns on the synthetic units). The first lane's CPU, 2, has the
  performance governor and energy preference; the others, powersave and
  `balance_performance`. So the lane that takes the largest body may take
  twice as long as the first would: two equal bodies of 12,485 and 24,965
  tokens each read 3.1 and 1.6% slower on two lanes than on one, where sixteen
  bodies of 993 tokens (16,091 in all) read 0.8% faster, and 3.7% faster
  without headers.

The reader runs one lane, and one more per `BUSTER_CHECK_DEFERRED_LANE_TOKENS`
of the bodies' tokens beyond twice the largest body's: 12,288, between the
most of those tokens on which a second lane lost (7,553: +1.5%, and +1.1%
after thirty headers; 10,185: +0.3%) and the fewest on which it paid (13,902:
−3.7%; 14,105: −0.8%, and +0.2% after thirty headers). The lanes it takes
read within 1% of the grid's best on every unit but six. It forgoes the gains
of four, which read faster on two lanes than on one: `00204.c` (2,114 tokens
beyond twice its largest body) by 3.5%, sixteen bodies without headers (3,710)
by 3.8%, and one body of 19,973 tokens beside fifteen of 1,981 (9,945) by
3.3%, of 9,989 beside fifteen of 993 (5,109) by 1.3%. And it takes part of two
gains: sixteen bodies of 1,981 tokens read 4.9% faster on four lanes and 3.6%
on its three, and one of 9,989 beside fifteen of 1,981 4.9% faster on three
and 3.8% on its two.

Then the rule against main's, each on eight lanes against one lane in the same
process, the medians of five rounds (main's rule is the same binary's with
`--every-lane`), in nanoseconds per token:

| Unit | Bodies' tokens beyond twice the largest's | The rule's lanes | Main's rule, 8 against 1 | The rule, 8 against 1 | Main's rule on 8 | The rule on 8 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| `00213.c` (5 bodies) | 0 | 1 | +51.8% | +0.3% | 346.00 | 219.52 |
| `00200.c` (2) | 0 | 1 | +18.7% | +0.0% | 80.80 | 66.94 |
| `00182.c` (11) | 346 | 1 | +23.2% | −0.0% | 109.44 | 86.08 |
| `00204.c` (108) | 2,114 | 1 | +9.7% | +0.1% | 103.60 | 91.91 |
| `sqlite3.c` (2,608) | 497,917 | 8 | −12.5% | −12.8% | 105.62 | 105.24 |
| two bodies of 12,485 tokens | 203 | 1 | +10.7% | +0.6% | 80.69 | 71.70 |
| sixteen of 993 | 14,105 | 2 | +4.4% | −0.3% | 76.09 | 71.64 |
| sixteen of 1,981 | 27,937 | 3 | −1.8% | −3.9% | 69.98 | 67.89 |
| one of 19,973, fifteen of 4,009 | 40,365 | 4 | −6.2% | −7.9% | 70.83 | 69.01 |
| sixteen of 4,009 | 56,329 | 5 | −7.8% | −7.6% | 67.33 | 67.15 |
| sixteen of 8,013 | 112,385 | 8 | −10.8% | −10.7% | 69.35 | 69.45 |

Of all 35 units, the rule's reading on eight lanes came within +0.7% of its
reading on one, and within +0.3% on all but three, which it reads on one lane
both times; main's was up to 52% slower. The rule's eight lanes cost no more than
main's on any unit beyond the runs' spread (+0.1% on the units both read on
eight lanes), and on `00182.c` and `00204.c`, 21% and 11% less. On
c-testsuite's 220 programs, in one round, main's rule read 71 more than 2%
slower on eight lanes than on one, 50 more than 5% (`00215.c` 80%). The rule
reads every one of them on one lane, so its two readings are the same work:
they came within 0.9% of each other on the 62 programs of 1,000 tokens or
more, and within the noise of readings of tens of microseconds on the others
(a standard deviation of 2.9% under 100 tokens).

## What is left

The top level, the merge and the phases after it are sequential, so the lanes
shorten only the bodies' walks, which on `sqlite3.c` are a sixth of the reading. Bodies that do what the merge doesn't take
(above) send their unit to source order. Intrinsic declarations, block-scope
declarations with linkage and transparency made in a body are the next to
merge.

A lane's cost decides how many lanes pay. Its copy grows with the lanes that
copy at once: each lane maps and advises five arenas at every reading and
faults their pages in afresh, and on the laptop perf shows the lanes' `mmap`
and `madvise` calls waiting on each other for the address space's lock. Arenas
kept across readings would spare that. And the costliest part goes to
whichever lane takes first, which may be a woken one: two lanes read
`00200.c`, a body of 20,420 tokens beside one of 30, 19% slower than one did.
That is why the rule counts the largest body twice. Leaving the largest bodies
to the first lane would let a unit of one large body beside many smaller ones
take the lanes it pays for, which the rule now forgoes. A unit the rule reads
on one lane still goes through `lane_run`, which pins its lane and puts its
affinity back: 0.6 to 7.5 µs of the dispatch on the 9700X, which a direct call
would spare.

The implementation, fixtures and documentation are original project work.
Clang is run externally as an oracle; no compiler implementation or external
test corpus is copied into this change.
