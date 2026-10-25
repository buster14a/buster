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
3. **The lanes.** `lane_parts_by_cost` cuts the bodies into parts by token
   count and each lane takes parts with `lane_take`, costliest first. A lane
   copies the top level's reader once (`check_deferred_lane_prepare`): its
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
     (`check_deferred_visible`): a tag first named in a file-scope declarator
     (`void g(struct S *p);`) ends with it in source order's reader, though
     its event's depth is 0, which keeps it in the history;
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

- `./build/compiler test-checker-deferred [--keep]`: 37 fixtures of the
  issue's cases, each written to a file and checked as `test-checker-corpus`
  checks one: on the scalar path, the vector path, and the vector path with
  every body read apart, on 1, 2, 3 and 8 lanes through the stacks, the batch
  and the direct binding resolution. The deferred result must be the vector
  path's column for column, and each fixture says whether the reader merges it.
  Each fixture that merges runs again on 1 and 8 lanes with its merge failing
  right after the replay (`CheckDeferred.fail_after_replay`), which must give
  source order's result.
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
  and a definition's parameter, and one declared before its prototype;
  errors in a body, at the top level, in the syntax and in a later phase; and
  400 bodies that call each other. Mutants that skip a lane's withdrawals
  when it moves back, or its restores when it moves on, fail 12 and 9 checks;
  one without the restores' check of the history's declarations fails the
  five tags' fixtures, and one without the table's reset 19 fixtures.
- `./build/compiler test-checker-corpus --deferred N [--deferred-bindings
  stacks|batch|direct] ...` adds the deferred reader on N lanes as a third path
  to each file's comparison, which must give the vector path's columns; a
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
LIST] [preprocessor options] file...` preprocesses each file once, then per
pass parses it and checks it by source order's `check_input` and by the
deferred reader on one lane and on `--lanes` (8), in turns that rotate. It
reports medians in nanoseconds per token: the parse; source order's reading;
and each deferred reading's whole, its top level, the history's index, the
bodies' walks and their position checks (summed over the lanes), the lanes'
span, the merge and the rest (the phases after the merge); the position checks
per body too. `--cpus` pins the command to the first CPU, where the parse,
source order and the top level run, and its lanes to the list.

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

## What is left

The top level, the merge and the phases after it are sequential, so the lanes
shorten only the bodies' walks, which on `sqlite3.c` are a sixth of the reading. Bodies that do what the merge doesn't take
(above) send their unit to source order. Intrinsic declarations, block-scope
declarations with linkage and transparency made in a body are the next to
merge.

The implementation, fixtures and documentation are original project work.
Clang is run externally as an oracle; no compiler implementation or external
test corpus is copied into this change.
