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
   `BUSTER_CHECK_DEFERRED_LANE_TOKENS` (1,280), and one more per
   `BUSTER_CHECK_DEFERRED_LANE_TOP_TOKENS` (3) of the top level's
   tokens, of the bodies' tokens beyond their critical path: the larger of the
   largest body, which the first lane reads itself, and twice the second
   largest, which a woken lane reads at about half the first lane's speed. It
   runs no more lanes than it is given or than bodies. A lane costs its copy of
   the reader, which grows with the top level, and its wake-up, which its share
   of the walks must pay for ("How many lanes", below).
   `CheckDeferred.every_lane` runs every lane the bodies can take, as the tests
   and `test-checker-corpus` do.

   The calling thread, the first lane, cuts the bodies into parts by token
   count before the lanes start (`parts_by_cost`: `lane_parts_by_cost`'s parts
   for a group of the lanes the rule gives, by one thread). The reader stably
   promotes the part containing its largest body to the front: the costliest
   aggregate part need not contain that body when several smaller bodies share
   a part. The first lane reads the reserved part at once, and every lane takes
   the others with `lane_take` in the remaining cost order, as it arrives: no lane
   waits at a sync for another. Before, the lanes cut the parts together, and
   the first lane waited at `lane_parts_by_cost`'s sync for the last lane to
   wake, which then took the costliest part from the counter: a woken lane
   reads at about half the first lane's speed.

   A lane copies the top level's reader once, when it takes its first part
   (`check_deferred_lane_prepare`), so that a lane that takes none copies
   nothing (#432's review): the per-tag arrays the walk writes, the scope
   stacks and reports, the appended tables, and the table (`type_copy`, with
   room for the largest body's types), whose columns a body's withdrawals
   change (step 4). The copies are columns with room for the largest body that
   a lane mostly never touches, so a lane's arenas are base pages throughout:
   the kernel clears a huge page whole the first time any of it is touched. The
   table's index has room for the top level's types and a slack more (at most
   4,096 and the top level's own count, and at least 512), and grows past them
   as any index does. Where the top level's index has that room already, its
   slots are copied as they are, rather than its types placed again.

   A lane copies none of the top level's per-name columns or binding events
   (#697's item 2). Its binding heads, parameters' and members' marks, stamps,
   linked declarations and types, and intrinsics' and builtins' caches are its
   own, in its sixth arena, at places they keep from one reading to the next
   (`check_deferred_lane_names`). Before its walk, a body sets them for the
   names it spells (step 4). After the walk it takes the ones that start as
   none back to none, so that no reading clears them. A reading with more
   names than the places have room for lays them out again, on zero pages, or
   on the old ones cleared where the kernel keeps them (it refuses
   `MADV_DONTNEED` for a locked range). Before its bodies, the pages of its
   names' entries are committed, each column's in one call
   (`MADV_POPULATE_WRITE`, `check_deferred_lane_commit`):
   a body's position checks set a few entries of every column for the names
   it spells, far apart, and their faults, a page at a time, took the position
   checks to up to 7 times main's on one lane and 14 times on eight ("A
   lane's copy at a first reading", below). A lane's binding stream holds
   proxies of the file scope's events its heads reach (step 4), then the
   body's own events. Before, it held a copy of every one of the top level's
   events.

   The state (`CheckDeferred`) keeps its memory (`CheckDeferredMemory`) from one
   reading to the next: its own arena, the top level's table and index, and
   each lane's six arenas, one mapping a lane, which the calling thread
   reserves before the lanes start, the first time the state runs that lane
   (`check_deferred_lane_arenas`). Each reading takes the memory back to empty,
   and `check_deferred_release` unmaps it. All of it is base pages: the top
   level's table has the caller's capacities, each column room for every type
   the unit could make, and the state's arena room for two linked updates a
   token, of which a reading touches a little at each one's start, where a
   huge page was cleared whole the first time (`check_deferred_arena`).
   Before, each lane mapped and advised its five arenas inside the lane at
   every reading, and lanes that did so at once waited for each other on the
   address space's lock. A state's reading after its first finds its pages in
   place, and a lane's copy is then a copy alone; a process's first reading
   still faults in every page its lanes' copies take.

   The bodies' walks write their entries of the reader's token columns into
   `check_input_run`'s arena of rare columns (#572), which every reading
   reserves anew, in huge pages past its scope stacks. Where the rule gives
   the bodies more than one lane, that arena is base pages throughout
   (`check_input_sparse_arena`, #764): lanes that write a 2 MB region of it
   that has no page yet, or the huge zero page where the top level's walk read
   it, each allocate and clear a huge page for it, of which the kernel maps
   one, and how often they meet there depends on their timing ("The rare
   columns' huge pages", below). The reader works out the rule's lanes from
   the parser's tree before its top level's walk (`CheckDeferred.planned`),
   and `check_deferred_bodies` works them out again from the top level's
   cuts. A reading on one lane keeps the huge pages, as source order does.
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
     the file scope the history holds (#438). Each head the history gives is
     one of the top level's events, which the body's stream gets a proxy of
     (`check_deferred_proxy`), in the keys' order, for the walk reads the file
     scope's events at its heads alone. After them comes a proxy of the top
     level's last event before the body, whose point a variable-length array's
     bound reads as the stream's last before the body's first own event
     (`check_input_vla_bound`'s `cached`), as in source order's stream there. The kept events' ids are mapped back to the top level's numbering:
     a proxy's to the event it copies, an own event's to its place past the
     top level's events;
   - each of those names gets the rest of the per-name state the top level's
     walk changes as it goes, as it was at the opening
     (`check_deferred_name_state`): its linked declaration and type, the last
     of the top level's updates before the opening (`check_deferred_linked`
     logs them by position, and `check_deferred_bodies` indexes the log by
     name). An upper-bound binary search selects the last equal-position update
     at or before the opening, so many later redeclarations do not make each
     body scan the name's suffix again. It also gets its intrinsic's cache entry, a hit not yet decoded where the
     top level decoded it after the opening, as source order's is there, and its builtins' cache entry, the top
     level's;
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
   its appended entries (its intrinsic declarations and calls among them), the
   completion positions and transparency of the tags it made, those of the
   top level's tags it completed (its definition's parameter-list tags, #436),
   the top level's VLAs it changed (its parameters') and the predefined types
   it made. Then the lane takes its names' heads and marks, its view and the
   top level's tags it changed back (`type_tag_restore`).
5. **The merge** (`check_deferred_merge`), in source order, on the caller's
   thread but for its pass, which runs on the lanes the bodies ran on:
   - `type_replay` makes the top level's changes and each body's again in the
     caller's table, merged by key (position, then order at the position), the
     top level's first at a position both have. Each change's types are mapped
     to the replay's, and `last_error` is the last write's by key;
   - each appended table is laid out with each body's entries at its cut, where
     source order appends them, and every id into it mapped;
   - the events are laid out the same way, each body's at its cut, and the
     indices into them that the walk keeps (each name's head and the top
     level's active declarations) mapped;
   - the pass (`check_deferred_pass_items`) shares the unit among the lanes by
     whole items, each the top level's tokens and events after one body and
     then the next body's own, by their tokens (`lane_range_by_cost`). Each
     item's events go to their places in the merged stream, the fields without
     an index copied whole and the handles mapped, a restore's mark past the
     top level's active declarations at the cut; and every column the walk
     wrote with a `TypeId` or a table id is mapped at the item's tokens that
     have an entry (`check_deferred_entries`, 64 tokens a test, with its
     scalar twin) through its owner's part: a body's tokens and its
     parameters' through the body's, the rest through the top level's. Every
     item writes places of its own, so the lanes need no sync until the pass
     ends, and one lane runs it on the caller's thread;
   - the tags' completion positions, before the pass, and linked types,
     predefined types (the earliest maker's by position) and `__func__`
     strings, after it, are mapped too;
   - the intrinsic declarations go in the order source order decodes them,
     each catalogue entry once, at its first maker's place: for each body, the
     top level's made before its cut, then the body's own, and last the top
     level's after the last cut. A body that uses an entry the top level
     decoded before its opening finds that declaration in its cache and makes
     none; one whose entry the top level decoded only after its opening
     decodes it itself, as source order's walk does there, and every later
     declaration of the entry, the top level's or another body's, is its own
     in the merge. The calls go at their cuts, each with its declaration's
     place, and each token's call id is mapped to its call's place.

Each body's walk writes the shared per-token columns only at its own tokens
and its parameters', which no other walk writes; everything else it writes is
its lane's copy. ThreadSanitizer found one place that read another body's
cells: `check_input_vla_parameters` read the body column of every VLA's
declaration. It now reads only the body's own parameters', the only ones that
can match. With that, ThreadSanitizer reports nothing on the fixtures,
c-testsuite, SQLite and Lua.

A lane also kept the top level's `enum_fixed`, which says per tag whether an
enum has a fixed underlying type (C23's `enum E : T`), until the lane-arenas
work found it: a body's tags take the indices after the top level's, as the
tags of the bodies its lane read before it did, so a plain enum, struct or
union there took an earlier body's fixed enum's state, and lanes wrote the one
array at once. No fixture had such an enum in a body before. Each lane now
reads with its own copy (`check_deferred_lane_prepare`), and every tag's state
is set where it is made (`check_input_tag_identity`).

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
- a body's declaration with linkage (`extern`, a function at block scope)
  that changes what later declarations see (below): its entity's first
  declaration, one that changes the entity's type, or one unlike the linked
  declaration it replaces;
- a body makes a union of the top level's transparent, which applies to what
  follows it: a file-scope union a block's typedef makes so, or a definition's
  parameter-list union its body defines so;
- a body's walk reports an error (a later phase's error is merged like any
  other: the phases run on the merged state);
- a table runs out of room.

A body's declaration with linkage is the file-scope entity's, and source
order's walk keeps it for the declarations after it: the name's linked
declaration (`linked_declarations`) and type (`linked_types`, the composite of
every declaration so far). A lane starts from the top level's at its opening,
and takes a declaration that leaves them as they would be without it: of an
entity declared before the opening, whose composite with the entity's type is
that type (no array size or prototype the entity lacked), and that replaces the
linked declaration only by one later declarations inherit the same from (its
kind, linkage and thread-local storage it must share, or the walk reports it,
and the rest it inherited, but whether it is naked, which is its own).

An intrinsic's tables are its declarations, decoded once per catalogue entry
in the order the walk first uses each, and its calls; the merge orders both as
source order's walk makes them (above). A lane has room for the top level's
declarations and as many again, since a body decodes each entry once at most,
an entry the top level decoded after the body's opening among them.

A body that completes its definition's parameter-list tag, which it declares
again (#436), completes a tag the top level made. Nothing after the body can
name it: its scope ends with the body, and distinct tags are never compatible
(C17's rule, which the checker keeps: C23's tag compatibility, #133, would let
code after the body compare it with another tag of its name). So the merged
table, which `type_replay` completes at the body's definition, and the tag's
completion position are all that show it, and the merge takes the position
from the body's result. A lane gives the tag back to the top level's state,
incomplete, before its next body.

A unit with overloadable functions stays source order's. A lane would need each
set's index as the top level had it at the body's opening (each name's
candidates by signature, and whether the name is overloadable yet), and its
own candidates and calls laid out at its cut, as the intrinsics' are. None of
the corpora below has one: C code meets them mostly in clang's `<tgmath.h>`.

The fallback checks the unit from nothing, with the caller's table taken back
to how it came: a merge that fails after its replay has filled it.

The table's transparency is withdrawn as its completions are, so that a
lane's table is source order's at the opening, as `test-types` checks. The
walk reads transparency by position (`transparent_unions`), so no checker
test can tell the reader's withdrawal of it apart now: a mutant without it
passes every checker test.

## Tests

- `./build/compiler test-checker-deferred [--id-order N] [--keep]`: 111 fixtures, 96 of the
  issue's cases, 400 bodies that call each other, 8 of the lanes' rule and 6
  of grouped parts, each
  written to a file and checked as `test-checker-corpus` checks one: on the
  scalar path, the vector path, and the vector path with every body read apart,
  on 1, 2, 3 and 8 lanes through the stacks, the batch and the direct binding
  resolution, the issue's cases on every lane their bodies can take
  (`CheckDeferred.every_lane`). The deferred result must be the vector path's
  column for column, and each fixture says whether the reader merges it. Each
  fixture runs again with its identifiers' IDs in another order
  ([#153](https://github.com/buster14a/buster/issues/153): the driver's
  `--id-order` table, `check_driver_id_order`, from `--id-order N` on, 153 by
  default), on 8 lanes through each resolution: the three paths must still
  be alike, and the reader must merge it as before. A
  merged unit's bodies must run on the rule's lanes (`CheckDeferred.active`),
  which the reader must have planned before its top level's walk
  (`CheckDeferred.planned`), in as many parts as a group of them cuts
  (`CheckDeferred.parts`), its reader
  be copied by one of them at least and by no more (`CheckDeferred.copies`),
  and its bodies' tokens that pay for a lane (`CheckDeferred.lane_tokens`) be
  the rule's, worked out again from the unit's tokens outside its bodies. Its
  arena of rare columns must be base pages throughout exactly where those
  lanes are more than one, and source order's never
  (`CheckInput.rare_columns_shared`, #764); `check_input_sparse_arena`'s
  mappings are checked apart, by the kernel's advice for them (`nh` and `hg`
  in `/proc/self/smaps`) at their ends and on both sides of the prefix, with
  capacities of a byte, of an arena's 64 KB of base pages and a byte more,
  and of three huge pages and 17 bytes, none of them committed until written.
  Each
  of the issue's cases runs again on 8 lanes by the reader's own rule, which
  reads most of them on one. The rule's fixtures are functions of empty
  statements whose bodies have known tokens, in units of the tokens that pay
  for a lane in their unit (N, with a top level of five tokens a function),
  read by the rule alone, whose bodies, tokens, largest and second largest
  bodies and lanes must be the ones they were made with: bodies of N on each
  side of a second lane (N tokens beyond twice the second largest body's, and
  one fewer) and of an eighth (seven times as many, and one fewer); a largest
  body of four times N, which the first lane reads itself, beside one of N, and
  of a token fewer (two lanes and one: #448's rule, which counted the largest
  body twice, gave one); and a largest body of three times N beside one of
  twice N and one of N, and of a token fewer (three lanes and two). Their
  largest body must be the first lane's (`CheckDeferred.largest_lane`), also
  on every lane their bodies can take of 8 with the first lane starting only
  once every other lane has taken from the counter
  (`CheckDeferred.late_first`), as when the others arrive first. Grouped-part
  fixtures add 127/128/129 and 511/512/513 bodies, with 20 tokens in the first
  and 18 in each other body. They check placement under both the rule and all
  requested lanes with the first lane delayed. In the 129-body case the rule
  cuts 128 parts: an aggregate of two smaller bodies costs 36 tokens, so merely
  reserving the costliest part would leave the largest body to a woken lane.
  Each of the
  issue's cases that merges runs again
  on 8 lanes with the first lane taking every part (`CheckDeferred.hold_lanes`), when
  no other lane may copy the reader; on 1 and 8 lanes with its merge failing
  right after the replay (`CheckDeferred.fail_after_replay`), which must give
  source order's result; on 8 lanes by a state that read it before, on one
  lane and on eight with every byte that reading pushed in the state's memory
  poisoned (`check_deferred_poison`) and on eight as it left it, whose memory
  the reading must keep (`check_corpus_deferred_reread`), and on one with
  every lane's stamp then at its last (`check_deferred_stamp`), which each
  lane's next body wraps: the one lane's first body's names, stamped as the
  wrapped stamp's first, must be taken again; and with the
  allocation of the reader's state failing
  (`check_corpus_deferred_fail_state`), which must count as a resource failure
  (`test-checker-corpus`'s `resources`), not as a result that differs. The 400
  bodies are read again with the reader timing what its lanes cost
  (`CheckDeferred.timing`), on one lane and on every one of 8: one lane must
  wait for no other, and the launcher's dispatch and the wake-up must fall within
  the lanes' span. The one-lane reading also runs inside a live pinned team
  on two distinct CPUs, with an unfinished take and an impossible inner pin
  target. It must preserve Lane/taken/running and immediate affinity, keep
  the installed team, and reuse both original worker identities afterward.
  A copy whose wake-up counts from the launcher's call, and one
  whose dispatch is the lanes' span, each fail it. Then
  `benchmark-checker-deferred`'s `--cpus` parser, into room for 64 CPUs that
  ends at a `PROT_NONE` page: lists of 1 to 64 are read whole, one of 65 is
  refused without a write past the room, and so are malformed lists; and the
  state's memory (`check_deferred_test_memory`): a lane's six arenas in one
  mapping, end to end with their capacities, writable to their last byte and
  kept with their pages when the state takes them back, the lanes' mappings
  and the state's own three arenas in base pages to their last byte, and
  every mapping gone after `check_deferred_release`. Then a lane's per-name
  columns (`check_deferred_test_lane_names`): laid out from room for 4,096
  names, in order, each on a line of its own and clear of the next, the pages
  of the reading's names' entries resident before anything reads them and the
  last page of each column's room not (where the kernel takes
  `MADV_POPULATE_WRITE`), those that start as none zero; at the same places,
  with what the last reading left, for a reading of fewer names; those that
  start as none cleared, with the stamp, after a poisoned reading, and the
  others left to the bodies, which set them before they read them; laid out
  again on zero pages for more names than the room, their entries committed
  again, and on the old pages cleared where the kernel keeps them (the names
  arena's first page locked, where the test may lock one); and refused for more
  names than the arena holds.
  Mutants of the change, each built at `-O1` from it merged with main and run
  on the 9700X (the record's `logs/mutants2`), fail. Of the rule: the largest
  body counted twice, as #448's rule did, 27 checks; the largest body alone
  24; the top level's share dropped 708; a ceiling's division 60; and
  `every_lane` not read 473. Of the lanes' changes: the parts' order with its
  first part first rather than its costliest, `test-lanes` (55 to 71 parts'
  checks wrong on each lane count); `parts_by_cost` cutting for a lane more
  than it is given, `test-lanes` (39 to 45) and 168 checks; `parts_by_cost`
  without its bound at the most lanes a group has, `test-lanes`, whose 257
  lanes' parts then overrun `lane_parts_order`'s stack (the stack protector
  aborts it); every lane taking from the counter, the first lane too, 9
  checks; the parts cut for every lane the reader is given rather than the
  rule's 18; the lanes' arenas not reserved before the lanes 974; a release
  that doesn't unmap them 723; a state that doesn't keep the lanes' memory
  554; the lane's name stamps not cleared 8; and the deferral mask not cleared
  hangs the reread of a poisoned state's memory past a 120 s timeout. Item
  2's three mutants (#697), each built with clang 18 on a host of the scalar
  path, fail too. The stream's last proxy taken from the top level's last
  event, rather than its last before the body, fails the two fixtures of
  bounds of file-scope names: they aren't merged, by any resolution on any
  lane count. The stamps not cleared where they wrap fails the reread with
  the stamps at their last on 12 fixtures (6 results that differ, 6 not
  merged), and the test then faults on a head that a skipped name left. The
  poisoned columns not cleared fails the per-name columns' check. So do its
  pages' three, built the same way: the state's own arenas in huge pages fails
  the memory test, and the names' entries not committed, or the heads' alone
  not, the per-name columns' check.
  They are: the issue's three (a later declaration, a later completion, an
  enumerator from an initializer); typedefs shadowed and redeclared after a
  body; tags completed, redeclared and shadowed around bodies, and completed
  between bodies where the walk itself needs them complete or not (a local
  object, local records, an enum, a typedef); block-scope declarations with
  linkage, which merge where they leave the entity's linked state as it was
  at the opening (after its definition and before it, a prototype before the
  function's definition, an array without the size the top level gives it
  later, a naked prototype's attribute it inherits) and go to source order
  where they don't (its first declaration, a size or parameters it lacked, the
  naked attribute it adds, a conflict); intrinsics in a body, in two,
  before the top level's use of them (with types the top level makes between
  them) and after it, and of vector types, which merge;
  strings and `__func__`, string initializers' indices, statement
  expressions, VLAs (bounds of file-scope names in bodies without parameters
  among them), varargs, a predefined type first made in a body, asm
  statements, cached constants and `_Generic`, compound literals and statics;
  recursion and forward calls, labels; unions made transparent in a body
  (a file-scope one, which goes to source order, and the body's own, which
  merges) and at file scope between bodies; naked functions' bodies, among
  others' and
  with what a naked body refuses; loop hints, and those clang refuses;
  tags first named in a prototype's parameter, a sizeof in an array's bound
  and a definition's parameter, and one declared before its prototype; a
  definition's parameter-list tag used in its body, and completed there (a
  struct, a union and an enum, after a use, and in each of two bodies), which
  merge; C23's enums with a fixed
  underlying type in bodies beside plain enums, structs and unions, read with
  `-std=gnu23`: a fixed one before a plain one and after it, eight bodies of
  them, and a file-scope one shadowed in a body;
  errors in a body, at the top level, in the syntax and in a later phase, and
  warnings inside nested selections in three bodies; and
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
`--deferred 8` and the IDs in another order (`test-checker-corpus --id-order
153`), and the frozen SQLite source's check with `--deferred 8`.

On the 9700X, with #697's item 4 (the bodies above merged) and `--deferred 8`,
each against clang 23.1.1: c-testsuite on the system's headers, as the 9700X
job checks it, and the frozen corpora on their own headers
(`--no-default-config --target=x86_64-linux-gnu -march=znver5 -nostdinc`):

| Corpus | Units merged | Left to source order | Bodies read apart | Differences |
| --- | --- | --- | --- | --- |
| c-testsuite's 220 programs | 220 | 0 | 449 | 0 |
| the frozen `sqlite3.c` (`-std=gnu17`, with `--symbols`) | 1 | 0 | 2,608 | 0 |
| the frozen musl's `src` (1,524 files, its build's options) | 1,486 | 38: 37 errors in a body, 1 that doesn't parse | 21,918 | 0 |
| the frozen Zig 0.14.1 `lib/libc` (2,315 files, 968 that don't preprocess alone, `-std=gnu17`) | 1,022 | 325: 256 that don't parse, 43 errors in a body, 26 at the top level | 3,178 | 0 |

Before it, on the laptop (Zen 4), with `--deferred 8` unless said, each
against clang:

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
LIST] [--every-lane] [--warm] [preprocessor options] file...` preprocesses each
file once, then per pass parses it and checks it by source order's `check_input`
and by the deferred reader on one lane and on up to `--lanes` (8), in their six
orders, one a pass in turn: in any six passes in a row, each reading comes
first, after the parse, twice, and after each other reading twice, so
`--passes` is a multiple of six, 24 by default. A deferred reading ran up to 3%
faster after the deferred reader's own than after source order's. The three
orders that rotated before put the reading on `--lanes` after the one on one
lane in two passes of three, and the six in 21 passes, as #448's review
counted, put it there 8 times and after source order's 6. The deferred
readings share one state, released after each reading, as a process's one
reading is; with `--warm` the state keeps its memory from one reading to the
next, as a process's later readings do. It reports the bodies, their tokens,
the largest body's and the largest of the others' (`second_body`), the top
level's tokens and the bodies' tokens that pay for a lane by the rule
(`lane_tokens`), and medians
in nanoseconds per token: the parse; source order's reading; and each deferred
reading's whole, its top level, the history's index, the bodies' walks and
their position checks (summed over the lanes), the lanes' span, the merge and
the rest (the phases after the merge); the merge's parts (its replay, the
appended tables, the events' layout, the tags' completions, its pass and the
rest); the position checks per body too, the
lanes the bodies ran on (`active`: as many as the rule gives them, or with
`--every-lane` every lane they can take) and those that copied the reader
(`copies`); and what a lane costs, in nanoseconds: a copy of the reader
(`copy`); the launcher's dispatch, from its call to the first lane's arrival
(`dispatch`: on multiple lanes, the first lane's pinning and the others' wake
calls); the others' wake-up, from the first lane's arrival to the last one's
(`wake`, none on one lane), which was the first lane's wait at
`lane_parts_by_cost`'s sync until the parts were cut before the lanes, and
which no lane waits for now; the lanes' arenas, reserved or taken back on the
calling thread before the lanes start (`arenas`); and the passes in which the
first lane read the largest body (`largest_first`). Before #448's review,
`wake` was the first lane's whole time in `lane_parts_by_cost`, its share of
the boundaries and its order of the parts with its wait, and left the dispatch
out. `--cpus` pins the command to the
first CPU, where the parse, source order and the top level run, and its lanes
to the list. The measurements before "How many lanes" were made with the three
rotating orders.

One active lane uses the existing `lane_run_cold(1, ..., NULL)` utility. It
starts no worker, makes no affinity calls, and saves/resets/restores the
calling thread's lane, counter and running state with a fresh group. It
leaves an installed team untouched. Multiple lanes keep `lane_run` and the
original CPU list; generic pinned-team and nested-dispatch validation is
unchanged. In particular, generic `lane_run(1, ..., NULL)` on a pinned team
is still refused. Only the checker's known one-lane entry takes this private
isolated path. Arrival/wake accounting and the adaptive rule are unchanged.

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

The campaigns below measure the hand-off and merged `54a3b6da` revisions.
They do not retime the later grouped-part placement correction described
above. Strict fit counts and sums use the corrected full-precision report;
ratios and medians remain unrounded until the final display. "Within one
point" means no more than one percentage point above the grid's best change
against one lane, and gains forgone sum those differences.

The later placement correction was compared without timing against all 43
archived inputs. Reconstructed token counts, body counts, total body tokens,
largest and second-largest bodies, and top-level tokens match the recorded B2
headers exactly. The reconstructed part-order arrays are unchanged in 422 of 430 configurations
(1/2/3/4/8 requested lanes, under the rule and with every lane forced). All
eight changes are in `h64x3.c`, whose largest bodies tie: the former first
part groups header helpers, while the corrected order reserves the part of
the first largest body. The other 42 inputs, including SQLite and the four
c-testsuite programs, retain identical orders. This is a scheduling identity
check; it supplies no new runtime measurements for the correction. A screen
of `h64x3.c` alone then timed it on the 9700X, before the correction and
after (the record's "The fix for grouped parts"), by the rule on 8 lanes,
which takes 3, and with every lane on 2, 3, 4 and 8, five rounds of 24
passes: the first lane read the largest body in all 120 passes of each,
where it read it in none, and each change against one lane stayed within
the rounds' spread of the other build's (by the rule, −1.6% against −0.5%).

A lane costs its copy of the top level's reader and its wake-up, which only its
share of the bodies' walks can pay for. On the 9700X, with `--cpus
2,0,1,3,4,5,6,7` and the frozen corpus's options, each unit and lane count in a
process of its own whose deferred readings each release their state (a
process's first reading, the default), three rounds, on 43 units: four of
c-testsuite's programs (`00182.c`, `00200.c`, `00204.c` and `00213.c`), the
frozen `sqlite3.c`, and 38 synthetic units of 2 to 70 bodies after `stdio.h`,
`stdlib.h` and `string.h`, after thirty of the C library's headers or after
none, among them one large body beside smaller ones and two large bodies of
different sizes ([the corrected record](https://github.com/buster14a/buster-performance/tree/e4ae6019ed75575d49f9e75fba752820a1da9050/docs/measurements/deferred-lane-costs-113-2026-10-27)):

- **A lane's copy** (the median of the 42 units but `sqlite3.c`, then
  `sqlite3.c`'s): when each lane reserved its five arenas inside the lane, 101
  µs on two lanes and 264 µs on eight (457 and 868 µs). Reserved before the
  lanes start, 69 and 102 µs (420 and 697 µs), and the reservation itself
  (`arenas`) 2 to 14 µs on two lanes and 4 to 20 µs on eight, on the calling
  thread. A state's later readings (`--warm`) copy into pages in place: 6 and
  9 µs (137 and 466 µs). A process's first reading pays for the first touch of
  every page its copies take, which grows with the top level: on two lanes,
  sixteen bodies of 993 tokens take 34 µs with no headers before them, 61 µs
  after three and 121 µs after thirty, and `sqlite3.c` 420 µs.
- **The wake-up:** the last lane arrives 15 to 17 µs (the medians on two and
  eight lanes) after the first, which before this change waited for it at
  `lane_parts_by_cost`'s sync and now reads the costliest part meanwhile.
- **A woken lane's walk** of a body token takes 1.8 to 2 times the first
  lane's: the first lane's CPU, 2, has the performance governor and energy
  preference, the others powersave and `balance_performance` (#448's record).
  With the race for the costliest part (#448's code with the first lane's take
  as the only change, the hand-off's campaign of 2026-10-26), two lanes read
  `00200.c`, a body of 20,420 tokens beside one of 30, 19.8% slower than one,
  and one body of 19,973 tokens beside fifteen of 265 11.0% slower, where with
  the first lane reading the costliest part they read 2.3% slower and 1.1%
  faster; the first lane read the largest body in 0 to 24 of 72 passes, and
  then in all of them.

The reader runs one lane, and one more per `BUSTER_CHECK_DEFERRED_LANE_TOKENS`
(1,280; 1,024 before #764) and one per `BUSTER_CHECK_DEFERRED_LANE_TOP_TOKENS`
(3; 4 before #764) of the top level's tokens (`CheckDeferred.lane_tokens`), of
the bodies' tokens beyond
their critical path, the larger of the largest body's and twice the second
largest's: the first lane reads the largest body at full speed, and a woken
lane the second at about half. On the grid of every lane count from 1 to 4
and 8 (`--every-lane`), the median change of each unit's reading against one
lane in the same process, by rule (lane counts between 4 and 8 interpolated):

| Rule: one lane, and one more per | Worst unit against one lane | Units within one point of their best | Gains forgone, summed |
| --- | ---: | ---: | ---: |
| 12,288 tokens beyond twice the largest body's (#448's) | +0.0% | 20 of 43 | 99.0 points |
| 8,192 tokens beyond twice the largest body's | +0.0% | 24 | 78.8 |
| 8,192 tokens beyond the largest body's | +5.5% | 24 | 59.3 |
| 8,192 tokens beyond the larger of the largest's and twice the second's | +0.0% | 28 | 48.6 |
| the same, 6,144 tokens | +0.9% | 35 | 29.3 |
| 1,024 tokens and a quarter of the top level's, beyond the same (the rule) | +0.0% | 42 | 9.2 |

Campaign A2, the same grid on this change merged with main (`54a3b6da`, beside
that main, `cd9c5c9b`), agrees: a lane's copy takes 69 and 100 µs on two and
eight lanes (main's 103 and 261 µs) and 6 and 9 µs at a later reading, and the
rule puts 38 units within one percentage point of their best and forgoes
10.7 points, where #448's rule puts 19 and forgoes 107.4. Campaign A4, the same
grid after #697's item 2, keeps the rule's constants ("A lane's copy at a first
reading (#697)", below). Campaign A5, after #764 put the rare columns of a
reading on more than one lane in base pages, fits them again: 1,280 tokens and
a third of the top level's ("The rare columns' huge pages (#764)", below).

A count of one size for every unit can't fit both ends: sixteen bodies of 265
tokens with no headers read 9.6% faster on four lanes than on one, and sixteen
of 525 after thirty headers 0.8 to 3.4% slower on any more lanes than one. The
rule's count is 1,073 tokens on the first and 9,035 on the second, about 3,300
after three headers and 38,802 on `sqlite3.c`, which takes eight lanes either
way. Neighbouring counts fit the grid as well, within its noise: 1,536 tokens
and a fifth of the top level's, or 1,024 and three tenths, read every unit
within +0.0% of one lane and put 41 or 40 within one point of their best on
campaign A (37 or 38 on A2), respectively. A state's later readings, whose
copies take a tenth of the time, would pay for a lane at a fraction of these
counts: 256 tokens a lane, whatever the top level, read every unit of campaign
A's warm grid (measured on 2 and 8 lanes alone) within +0.0% of one lane and
forgo 14.8 points, where the rule forgoes 44.8. On the merged A2 warm grid the
corresponding sums are 13.1 and 47.1 points.

Then the rule against main's, each on eight lanes against one lane in the same
process, on this change merged with main (`54a3b6da`) against that main
(`cd9c5c9b`), the medians of five rounds (their lowest and highest in
parentheses), and the readings on eight lanes in nanoseconds per token (the
record's campaign B2; its campaign B, three rounds of the same comparison on
the hand-off's base, main `43030b89`, gives every unit the same lanes, its
medians within 1.7 points of these):

| Unit | Main's lanes | The rule's | Main's rule, 8 against 1 | The rule, 8 against 1 | Main's on 8 | The rule's on 8 |
| --- | ---: | ---: | --- | --- | ---: | ---: |
| `00213.c` (5 bodies) | 1 | 1 | +0.0% (−3.4 to +3.5) | +0.1% (−1.8 to +0.3) | 206.15 | 204.01 |
| `00200.c` (2: 20,420 tokens and 30) | 1 | 1 | +0.3% (−0.3 to +0.9) | +0.5% (−1.1 to +1.2) | 55.07 | 54.81 |
| `00182.c` (11) | 1 | 1 | −0.2% (−0.5 to +0.3) | +0.2% (−0.6 to +0.8) | 69.33 | 67.87 |
| `00204.c` (108) | 1 | 2 | +0.4% (−0.4 to +0.5) | −3.3% (−4.6 to −2.5) | 72.94 | 70.35 |
| `sqlite3.c` (2,608) | 8 | 8 | −14.0% (−14.2 to −13.9) | −15.0% (−15.3 to −14.6) | 82.74 | 81.51 |
| sixteen bodies of 265 tokens, no headers | 1 | 4 | +0.1% (−1.1 to +0.8) | −9.9% (−10.6 to −8.7) | 60.12 | 54.50 |
| sixteen of 993, no headers | 2 | 8 | −9.5% (−10.3 to −6.8) | −12.7% (−17.0 to −12.2) | 43.10 | 41.38 |
| sixteen of 265 | 1 | 2 | +0.1% (+0.0 to +0.2) | +0.9% (+0.7 to +1.7) | 62.90 | 63.21 |
| sixteen of 993 | 2 | 5 | −0.4% (−2.5 to +1.1) | −3.9% (−4.4 to −3.2) | 57.06 | 54.96 |
| sixteen of 4,009 | 5 | 8 | −7.7% (−8.3 to −7.2) | −10.9% (−11.5 to −10.5) | 54.50 | 52.47 |
| sixteen of 993, thirty headers | 2 | 2 | −0.2% (−0.8 to +0.7) | −0.1% (−1.7 to +0.5) | 68.16 | 67.36 |
| sixty-four of 161 | 1 | 3 | −0.0% (−0.1 to +0.2) | −3.4% (−5.8 to −2.7) | 57.31 | 55.10 |
| two of 12,485 | 1 | 1 | +0.5% (−0.6 to +0.7) | +0.3% (−0.3 to +0.8) | 56.75 | 56.26 |
| one of 19,973 beside fifteen of 265 | 1 | 2 | +0.8% (+0.3 to +3.4) | −1.4% (−2.0 to −0.0) | 57.52 | 55.94 |
| one of 19,973 beside fifteen of 993 | 1 | 5 | +0.6% (−0.4 to +1.1) | −6.3% (−6.8 to −5.6) | 59.08 | 55.21 |
| one of 19,973 beside fifteen of 1,981 | 1 | 8 | +0.3% (−0.5 to +1.3) | −8.1% (−9.1 to −7.4) | 60.77 | 55.48 |
| one of 24,965 and one of 12,485 | 1 | 4 | −0.1% (−0.8 to +1.3) | −2.9% (−3.9 to −2.7) | 58.29 | 56.53 |

Of all 43 units, the rule gives 28 more lanes than main's rule and none fewer:
on them, its eight lanes read −14.1 to +0.9% against one lane, where main's
rule read them −10.5 to +1.2%, and 3.9% faster than main's eight lanes (the
median; −9.3 to +0.5%). The three it reads slower on two lanes than on one,
by +0.3 to +0.9% (sixteen bodies of 265 tokens after three headers the most),
are within what the same work reads apart: the units both rules read on one
lane, whose two readings are the same work, came within −3.4 to +3.5% of each
other in a round. On c-testsuite's 220 programs (three rounds, campaign CT2), the
rule reads every program on one lane, as main's does, but `00204.c` (two lanes,
−2.8%); the two readings of the others, the same work, came within −0.7 to
+0.6% of each other on the 61 programs of 1,000 tokens or more (a standard
deviation of 0.24%), −0.8 to +3.0% on the 27 of 100 to 999 (0.72%) and −2.0 to
+2.6% on the 131 under 100 (0.84%). Its readings on one lane take less time
than main's, 4.1% (the median) of those under 50 µs and 1.4% of those of 200
µs to 1 ms: a lane's arenas are one mapping, reserved with one `mmap` and one
`madvise`, where main's lane made five `mmap`s and ten `madvise`s, though
builds differ by about 2% for their code's placement alone.

#### #448's rule

#448 fitted the rule this change replaces, from a grid of the reader on 2 to 8
lanes against one lane, on 35 units:
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

### The merge's serial work (#697)

On eight lanes on `sqlite3.c`, the merge took 7.3 of the reading's 73.5
ns/token. That is more than the 4.7 by which the reader trailed source order;
its pass over every token's columns took 5.1 of them. The pass now finds the
columns' entries 64 tokens at a time, and it runs on the bodies' lanes with the
binding events' copies (step 5 of "The reader"). On the 9700X, on the frozen
`sqlite3.c`, these are the medians of six processes of 24 passes each, in
nanoseconds per token ([the
record](https://github.com/buster14a/buster-performance/tree/5ee80035f62e5f8826407a5801cb4770a9f0ae81/docs/measurements/deferred-merge-697-2026-10-28)
has the method, the logs and the traces):

| | Before | With the scan | With the pass on the lanes |
| --- | ---: | ---: | ---: |
| source order's reader | 68.78 | 69.02 | 69.04 |
| the deferred reader, one lane | 86.36 | 84.49 | 84.73 |
| its merge | 7.02 | 5.16 | 5.04 |
| the deferred reader, eight lanes | 73.48 | 71.29 | 69.34 |
| its merge | 7.27 | 5.39 | 2.62 |
| its pass (before it, the token columns alone) | 5.14 | 3.26 | 1.65 |
| eight lanes against source order, each process's | +6.8% (+6.7 to +7.1) | +3.3% (+3.1 to +3.7) | −0.0% (−0.4 to +0.5) |

On eight lanes, three parts of the merge stay on the calling thread: its replay
(0.56 ns/token), its appended tables and tags' completions (0.28), and the
events' layout (0.06). One lane's pass, with the events' copies in it, takes
4.20 ns/token, against 3.13 for the columns and 1.26 for the events before.

The traces show what limits the pass. Each of eight lanes takes its 88,000 to
102,000 tokens in 0.9 to 1.1 ms, where one lane takes all 706,237 in about 2.1
ms, so the lanes share the memory's bandwidth ("What is left"). A cursor over
the bodies' parameters, in place of the owner lookup's walk, saved one lane 4%
of its token columns and eight lanes nothing. It was left out.

After items 2 and 4, on the same file, by the rule on eight lanes, six processes
of 24 passes each (campaign S4 in
[item 2's record](https://github.com/buster14a/buster-performance/tree/79c90346c27d937bb8dffb771be9d9fcca6995b6/docs/measurements/deferred-copies-697-2026-10-28)),
in nanoseconds per token:

| | Main (`abc6bf24`) | With item 2 |
| --- | ---: | ---: |
| source order's reader | 71.13 | 70.39 |
| the deferred reader, one lane | 86.52 | 83.82 |
| the deferred reader, eight lanes | 70.49 | 66.42 |
| its top level | 23.34 | 22.67 |
| the lanes' span | 5.50 | 5.24 |
| its merge (its pass, on the lanes) | 2.61 (1.66) | 2.44 (1.55) |
| `check_input_run`'s phases after the merge | 38.85 | 35.87 |
| eight lanes against source order, each process's | −1.1% (−2.2 to −0.3) | −5.6% (−5.9 to −5.0) |

On eight lanes the serial part, the top level, the history's index, the merge
but its pass and the phases after it, takes 59.6 of the reading's 66.4
ns/token: the lanes shorten 6.8 of it.

### A lane's copy at a first reading (#697)

At a process's first reading a lane's copy of the top level's reader was mostly
the first touches of its pages: 69 µs on two lanes and 100 µs on eight on
campaign A2 (the median of the 42 units but `sqlite3.c`), and 401 and 663 µs on
`sqlite3.c`. A lane now copies none of the top level's per-name columns or
binding events, and its type index keeps the top level's slots where they have
the room (step 3). On the 9700X, on A2's grid and units, three rounds (campaign
A4 in item 2's record), a lane's copy takes 41 and 71 µs (245 and 467 µs on
`sqlite3.c`), main's 72 and 125 µs (450 and 787), and at a state's later
reading (`--warm`) 3 and 4 µs (48 and 86), A2's 6 and 9 (140 and 457).

Two costs that the copy hid came out with it. The state's own arenas were huge
pages past their first 64 KB, of which a reading touches a little at each
column's start: in base pages, main's reader read sixteen bodies of 993 tokens
after three headers 27% faster on one lane and 43% faster on eight, and its
threads cleared huge pages in 63 of 836 cycle samples of six passes, where they
had in 414 of 1,299. And a lane's per-name entries faulted a page at a time in
the position checks, up to 7 times main's on one lane and 14 times on eight,
until the lane committed them (step 3). With both, the 43 units read 26.3%
faster than main on eight lanes (the median; 5.2 to 42.9%) and 23.6% faster on
one, and 1.9% and 1.3% faster than main with its state's arenas in base pages
(from 8.4% faster to 10.7% slower on eight). By the rule, which gives every unit
the same lanes in both, item 2 reads each unit 5.5% (`sqlite3.c`) to 43.1%
faster than main, and the 31 units it gives more than one lane read 23.3% faster
to 0.2% slower than one lane (the median −6.1%), where main's read them 20.2%
faster to 7.1% slower (−1.5%) (campaign B4, five rounds). On c-testsuite's 220
programs (CT4, three rounds), both rules read every program but `00204.c` on one
lane, and item 2 reads them 30.5% faster than main (the median; 8.6 to 44.5%).
`sqlite3.c` reads 1.2% slower on one lane than main with base-page arenas: a
body's position checks set the lane's own columns and proxies for each name it
spells, 6.5 ns/token on eight lanes, summed over the lanes, against main's 5.4
(S4, above).

The rule keeps its constants. On A4's grid it forgoes 48.5 points, the least of
the grid, puts 26 of the 43 units within one point of their best, and reads its
worst unit, sixteen bodies of 993 tokens after thirty headers, 1.7% slower on
its two lanes than on one, where B4's five rounds read it 0.2% slower. 1,536
tokens and a sixth of the top level's forgo 51.2 points (worst +0.7%), and
4,096 and a third, A3's best before the last two commits, 79.9 (+0.0%). Most of
what the rule forgoes is on units whose eight lanes read worse than four, where
the lanes' walks write into `check_input_run`'s arena of rare columns (#572),
huge pages past its scope stacks. Where the top level's walk only read a 2 MB
region of it, or never touched it, a lane's write allocates and clears a huge
page, and lanes that write a region at once each clear one, of which the kernel
maps one; whether a woken lane comes to a region with the first lane or after
it depends on the lanes' timing. On A4, eight lanes' walks of one unit took 57,
66 and 36 ns/token in its three rounds, against 28, 23 and 25 by main's reader
with base-page arenas, which runs into it too, and at a state's later readings,
whose lanes reach their walks at once, they run into it in every round. In base
pages that arena takes the race away but costs source order's reader, whose
arena it is, up to 6.6% on the record's units, where it saves up to 4.2% on
others: #764, below.

### The rare columns' huge pages (#764)

The campaigns below, their trees and their logs are in
[#764's record](https://github.com/buster14a/buster-performance/tree/cc514098c9ad2ad809cb1b8ef82f3c365991375a/docs/measurements/deferred-rare-columns-764-2026-10-29).
The race's time is the kernel's, clearing fresh memory. On the 9700X a first
write into a fresh 2 MB region takes 82 µs from the allocator's free lists,
and clearing runs at about 26 GB/s however many threads share it: two, four
and eight threads that write regions of their own at once take 153, 311 and
651 µs each. Threads that write one region at once each allocate and clear a
page, of which the kernel maps one. A cycles profile at a fixed period
(`perf record -c 200003`, whose samples count cycles) has main's woken lanes,
in eight-lane readings of `b16x77.c` whose walks raced (76 ns/token), spend
180 Mcycles in `clear_page_erms` over 24 readings, against 6 with the token
columns in base pages. A process of main's whose eight-lane walks raced (65
ns/token) took 57 ms more of CPU (`task-clock`) than one of the committed
columns' below (23 ns/token), whose walks took 65 ms less in all. Item 2's
record sampled at a frequency, and found the clears too few to account for
the walks.

Campaign D1 read diag9's six units, `00200.c` and `sqlite3.c` on eight lanes
with every lane, five rounds, by five trees: main; the arena in base pages
throughout for every deferred reading (#791's first head); its token columns
alone in base pages, for every deferred reading and only where more than one
lane reads; and the columns committed on the calling thread before the lanes
(`MADV_POPULATE_WRITE`). Main's eight-lane walks of `b16x77.c` took 24.5 to
53.4 ns/token over the rounds, and of `l16x38f384.c` 21.6 to 34.2; base pages
kept every unit's within 3.2. Base pages throughout read the bodies fastest of
the base-page trees: on eight lanes, `sqlite3.c` 5.7% faster than main,
`b16x77.c` 18.4% and `l16x38f384.c` 14.5%, and `h16x19.c`, `h16x10.c` and
`00200.c` (two bodies) 3.9 to 6.2% slower, where a lane's faults of 4 KB
pages, and the merge's pass and the phases after it, which read every token's
entries, cost more than the huge pages' clears. The columns alone in base
pages read every unit slower than base pages throughout. The committed
columns' walks were the fastest, but the phases after the merge then read
committed zero pages where they had read the huge zero page, which costs no
memory traffic: `sqlite3.c` read 4.8% slower than main. On one lane, where
nothing races, base pages read five of the eight units 2.6 to 6.2% slower,
and `sqlite3.c`, `b16x77.c` and `l16x38f384.c` 3.5 to 7.2% faster: the arena
keeps its huge pages there, as source order's does.

Campaign A5 read A2's 43 units five rounds, this change (`3b87d0c9`) on 2, 3,
4 and 8 lanes with every lane and, on 8, beside the base-page control (#791's
first head, base pages at every deferred reading) and main. The eight-lane
walks' spread over the rounds is a median of 1.40 ns/token over the units and
at most 4.94, where the control's is 1.12 and 5.68 and main's 1.33 and 37.79,
over 5 on twelve units. Against main, the change reads the units on eight
lanes 1.7% slower (the median; 22.0% faster to 9.5% slower), from the racing
units' gains to the losses of units of few or short bodies, which the rule
reads on one lane, and on one lane 0.4% slower (2.2% faster to 3.3% slower),
the same pages as main's. At a state's later readings (`--warm`, campaign W5,
diag9's units and `sqlite3.c` on eight lanes, five rounds), whose woken lanes
reach their walks at once, main's walks raced in every round, 52 to 69
ns/token on four of the units, and the change's took 12 to 33 there: it read
them 12 to 28% faster than main, `sqlite3.c` 5.3%, and `h16x10.c`, on which
main didn't race, 9.2% slower. On `sqlite3.c` by the rule, eight lanes, six
processes (campaign S5), the change reads 5.1% faster than main and 10.4%
faster than source order, where main reads 5.6% faster: its lanes' walks take
20.1 ns/token in all against 27.2, its lanes' span 1.0 less, and its top
level, merge and later phases 2.2 less. Source order, whose arena keeps its
huge pages, reads A2's units 0.5% slower than main by the median (0.8% faster
to 2.2% slower) and `sqlite3.c` 0.8% faster, in processes whose every reading
is on one lane (campaign SO5, five rounds). The two builds run the same code
there: their processes' user instructions differ by 0.002 to 0.005%, the
plan's sums over the bodies among them, and their cycles by 0.5 to 0.7%, the
builds' placement (campaign SOI, three processes of each on four of the
units). In A5's processes, beside readings in base pages, source order reads
1.1% slower, since its huge pages then come from the free lists rather than
from a huge-page reading's just freed.

A second lane's first touches of base pages cost more than of huge pages, and
more with a larger top level. On A5's grid the rule's constants, 1,024 tokens
and a quarter of the top level's, forgo 49.4 points and read the worst unit
4.8% slower on its two lanes than on one; 1,280 tokens and a third forgo 45.3
and read the worst unit 2.1% slower, and do better than 1,024 and a quarter in
four of the five rounds, so they replace them. They read every c-testsuite
program on one lane, `00204.c` too (2,282 tokens beyond its critical path,
5.4% faster on two lanes than on one). A sharper fit, 64 tokens and half the
top level's, forgoes 40.7 points, but 128 and a half forgo 52.3: the
difference is two lanes for `00204.c` and for `00213.c`, of 460 tokens.

By each build's rule (campaign B5, A2's 43 units, five rounds), the change
reads the units 0.6% slower than main by the median, from 22.9% faster to
8.6% slower. Twelve read more than 1% faster, `sqlite3.c` by 4.6%: their
lanes no longer race, and their top level's walk no longer clears a huge page
for each 2 MB region it writes (up to 5.4 ns/token faster). Twelve are within
1%, all but one on one lane in both. Nineteen read more than 1% slower:
fourteen units of two to eight lanes whose lanes didn't race in main, by 1.7
to 8.6%; four that the refitted rule reads on one lane where main's read them
on two (`00204.c` 5.3% slower); and `00213.c`, on one lane in both. On the
fourteen, in base pages, the merge's pass and the phases after it read the
arena's untouched pages a 4 KB fault at a time where main's read the huge
zero page, 0.6 and 0.9 ns/token more (the medians), and the lanes' span takes
0.9 more, as much on the five whose lanes the refit leaves as they were.

## One-lane pinning removed (#450)

The exact `35f26f68` baseline and `1d2b12c6` candidate were compared on the
9700X under its shared exclusive lock, with 24 passes, the six reading orders,
CPU list `2,0,1,3,4,5,6,7`, frozen SQLite/headers and unchanged options. Five
alternating process pairs covered 13 units, followed by a 219-program screen
and a predeclared five-pair repeat of every eligible corpus unit. All 2758
timed processes succeeded; 226 identities matched before/after. The six
measurement holds lasted 37/24/1/2/3/36 seconds, each bounded below the
approved 60-second cap. The [immutable evidence record](https://github.com/buster14a/buster-performance/blob/1294f0afc4f39db0d98630018e9808fde4c44ea3/docs/measurements/deferred-one-lane-450-2026-10-26/README.md)
contains exact sources, commands, raw logs, controls and the separate
confirmatory corpus results.

| SQLite reading or metric | Baseline | Candidate | Change of build medians |
| --- | ---: | ---: | ---: |
| Whole checker, one lane (ns/token) | 120.29 | 119.66 | -0.5237% |
| Whole checker, eight lanes (ns/token) | 104.09 | 103.84 | -0.2402% |
| Dispatch, one lane (ns) | 7204 | 391 | -94.6% |

One-lane whole paired changes are all negative (-0.8064% to -0.1917%);
eight-lane changes are mixed (-0.7550% to +0.1442%). Small-unit dispatch is
30 to 40 ns, with zero one-lane wake time. These whole-reader timings
exclude preprocessing and the separately measured parse; no instruction or
RSS improvement is claimed.

The original screen retained 36 rows above +2%, including both 3035-token
`00056.c` readings. The independent all-corpus confirmation has 33 positive
build-median rows, at most +1.5749%, and none worse in all five pairs. Its
largest paired median is +2.8326% with mixed signs. The original 26-token
`00066.c` one-lane median is +2.2499%, while confirmation is +0.8668% by
build medians and -0.4233% by paired median. All observations remain separate;
no normalization or pooling waives them. This protocol detects no repeatable
measurable regression and supports manual acceptance, with integration CI
still a separate gate.

Native code generation retains the common reader's 0x808 stack allocation;
its text grows 32 bytes. The compiler naturally separates the existing cold
utility, reducing the checker team's wrapper allocation from 0x2380 to
0x138, without an added helper or annotation. Earlier direct-context,
helper, outlined-phase and affinity-check attempts were rejected; their
[negative evidence](https://github.com/buster14a/buster-performance/tree/10b5cd568edc50d851fc93bfc28b9a1db9f7da8a/docs/measurements/deferred-one-lane-450-2026-10-26)
is retained.

## What is left

The top level, the merge's replay and layouts, and the phases after it are
sequential, so the lanes shorten only the bodies' walks and the merge's pass,
which on `sqlite3.c` are about a quarter of one lane's reading. On eight lanes
the pass shares the memory's bandwidth: it reads each of its columns at every
token, mostly zeros, and the lanes' scans of their shares run at about a third
of one lane's speed per token (on `sqlite3.c`, 0.8 ms a lane for 88,000
tokens, against 2.1 ms for all 706,237 on one lane). A record of where the
walk writes each column's entries, such as a bitmap of 64-token blocks for the
sparser columns, would let the pass read those blocks alone.

Bodies that do what the merge doesn't take
(above) send their unit to source order: on the corpora above, only bodies
that report errors do since #697's item 4. Overloadable functions and a union
of the top level's made transparent in a body are what the merge doesn't take
yet.

A lane still copies the top level's type table, whose withdrawals a body's
walk makes in place (step 4), and at a process's first reading the position
checks fault in the lane's event columns as they write the proxies: on eight
lanes, `sqlite3.c`'s position checks take 6.5 ns/token in all, against main's
5.4 with each lane's copy of the top level's events. Where more than one lane
reads the bodies, the checker's rare columns are base pages (#764), which the
lanes fault in 4 KB at a time and the merge's pass and the phases after it
read a fault at a time where nothing wrote them: units whose lanes didn't race
for huge pages read up to 8.6% slower than main by the rule. Huge pages that
the lanes don't race for (each 2 MB region faulted by the first lane to write
it, the others waiting for it), or a pass that reads only the blocks the walks
wrote (above), would take that back (#847). With the race gone, a state's later
readings can have a rule of their own (#697's item 3), fitted on a grid like
campaign W5's, once a caller reads several units with one state.

The implementation, fixtures and documentation are original project work.
Clang is run externally as an oracle; no compiler implementation or external
test corpus is copied into this change.

