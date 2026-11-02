# snapshot

Run commands and resolve source paths from the repository root. Follow the
[shared agent rules](../../AGENTS.md), including the 9700X lock and `cores`
requirements. [Command index](README.md).

## Module overview

- `src/buster/compiler/snapshot.c` is the runtime snapshot (step 5's fourth
  part, and step 6's for the other kinds): what `runtime_import` (or
  `runtime_import_static`, `runtime_import_static_pie` or
  `runtime_import_shared`) makes of the C runtime, kept in a file per kind in
  the cache directory (`$XDG_CACHE_HOME/buster`, or `$HOME/.cache/buster`),
  which `runtime_open` (or `runtime_open_static`, `runtime_open_static_pie` or
  `runtime_open_shared`) maps and uses in place when this executable wrote
  it, it is of the kind, its directory hashes right and every file the import
  probed is as it was, and otherwise rebuilds and renames into place. Binding checks each piece of it (a crt object or a member) when
  it first reads it (`runtime_piece_current`), and a link's room
  (`bind_capacity_needed`) reads only the directory: the pieces' marks and
  atoms' rooms (`RuntimePiece.atom_room`), never their records. `snapshot_test.c` holds its tests (`main.c` includes it;
  `./build/compiler test-snapshot` runs them) and `snapshot_benchmark.c`
  provides `benchmark-snapshot`.
## Snapshot tests

`./build/compiler test-snapshot [--seed N] [--functions N] [--directory PATH]
[--keep]` (about 0.31 s on the 9700X) checks `snapshot.c` with this system's
runtimes, a dynamic PIE's, a static executable's, a static PIE's and a
shared library's, in a directory of its own under `--directory` (`TMPDIR` or
`/tmp` by default), which it removes unless `--keep`:

- round trip: the imported runtime written as a snapshot and loaded back must
  have the import's records, every one that binding and a dynamic PIE read
  (the names' and atoms' bytes and the members' names through the loaded
  runtime's deltas, each record through the link that holds it), every piece
  current, every slice of a crt object that binding reads within one span of
  at most 16 KB after the directory and no member's there (#490), and the
  loaded runtime must use the file's mapping and no arena; so must it after
  another snapshot was renamed over the file it maps. Then the same for the
  static runtime
  (`runtime_import_static`), with its kind, its archives, its members'
  references (with the members they load) and COMDAT groups and its global
  names' members, for the static PIE's (`runtime_import_static_pie`), and
  for the shared library's (`runtime_import_shared`), its start object's
  piece empty;
- links, static links and static PIE links: test-link's two sets that call
  libc and a program of `--functions` workers (2,000 by default), bound by
  both of binding's paths against the imported runtime and against a loaded
  one, and against the loaded one again over a dirty arena (test-bind's, with
  the pattern left in `Bind.symbols`): the same links and bindings, written as
  dynamic PIEs, or against the static runtime as static executables and
  against the static PIE's as static PIEs, by 1 and 8 lanes into the same
  bytes, which run; and test-bind's loader case bound both ways too, the
  loaded runtime's PIE running with its status (11);
- opening: `runtime_open` in a directory two levels below one that exists,
  which it makes private: imported and written, then loaded; with no
  directory, or without an identity, imported and nothing written; and where
  the directory can't be made, the runtime still imported, the failure named;
- kinds: `runtime_open_static`, `runtime_open_static_pie` and
  `runtime_open_shared`, each imported and written, then loaded, with its
  kind's records; the dynamic runtime's snapshot beside them, the four
  (`runtime.snapshot`, `runtime-static.snapshot`,
  `runtime-static-pie.snapshot` and `runtime-shared.snapshot`) the
  directory's only files; each kind's snapshot loaded as each other kind,
  stale; and each kind's snapshot renamed to the next kind's name in turn,
  stale there, imported again and written over;
- staleness: snapshots with probes of the test's own files, each changed one
  way (rewritten in place to the same size, its mode changed, replaced by a
  rename, removed, created where a probe found nothing, a file added to a
  probed directory, written with its times put back), and one written by
  another executable, must be stale, and `runtime_open` must rebuild them;
- deferred probes: changed and newly created optional-library paths leave
  snapshot loading and no-use or weak-only links alone. Strong unwinder and
  split-stack references, undefined names (including shared-library names),
  and a needed library's version need request a snapshot retry. Both binding
  paths check the result, and discarding the snapshot re-imports current data;
- invalid files, of the dynamic PIE's runtime's snapshot and of the static
  PIE's: 1,600 damaged copies (cut short, one byte longer, a byte of the
  directory changed at sampled offsets and in every byte of the header), each
  made in place over the snapshot and repaired after its load, must be
  invalid, and so must 12 whose header was changed and then rehashed
  (another version or magic, a region misaligned, running or starting past
  the end or of the wrong size, counts out of range, more crt objects'
  records than a table has, the members' names placed before the crt
  objects' end, the directory ending before its last region), which only the
  header's own checks catch;
  `runtime_open` must replace them with the kind's runtime; a directory and
  a FIFO at the snapshot's path must neither block a link (run in a child
  under an alarm) nor fail it, the FIFO replaced and the directory left with
  nothing beside it;
- damaged pieces, of each kind's snapshot: a byte of the pieces' records
  changed at 171 offsets (at each 1/61 of them, and the first and last of
  each of their regions), each copy of which must load with every piece
  current but the one whose records hold the byte, by the test's own reading
  of the layout (none for a byte of padding or of a column no piece's hash
  covers), and give a link of every member's names the room
  (`bind_capacity_needed`) the whole snapshot gives it, which reads only the
  directory. Then a program bound against a snapshot with a damaged piece, by
  each path: `crt1.o`'s (or `Scrt1.o`'s, or `rcrt1.o`'s), `crtend.o`'s (or
  `crtendS.o`'s), or a member the program joins must fail the binding with
  `BIND_ERROR_SNAPSHOT`, and a member it doesn't join must bind as against
  the whole snapshot. After a failure, `snapshot_discard` and `runtime_open`
  import the runtime and write a snapshot that loads;
- concurrency: eight processes opening the runtime at once in an empty
  directory must each get it and leave one snapshot, which loads, and no other
  file;
- the cache directory: `snapshot_directory` from `XDG_CACHE_HOME` and `HOME`.

`--seed N` and `--functions N` change the programs. The 9700X job's test step
runs it.

## Snapshot benchmark

`./build/compiler benchmark-snapshot [--directory PATH] [--runs N] [--only
dynamic|static|static-pie|shared]` reports, for a dynamic PIE's runtime, a
static executable's, a static PIE's and a shared library's (`--only` one of
them), medians of `--runs` runs (41 by default) of the import without a
snapshot, `snapshot_write` into a directory under `--directory` (`TMPDIR` or
`/tmp`), `runtime_open` (or `runtime_open_static`, `runtime_open_static_pie`,
`runtime_open_shared`) from the snapshot phase by
phase (the compiler's identity, the mapping, the header and the directory's
hash, the view, the probes' `stat`s), `runtime_release` of a loaded runtime,
the alternatives the design weighed (the snapshot read with `pread` into new
memory, mapped without the hash, and mapped and hashed whole as format 3
checked it), and binding a program of one worker against the imported
runtime, against one loaded afresh each time (as a link binds, its pieces
faulted in and checked), against one loaded once, and the checks alone of the
pieces it read. Every binding must make the same link. Run it under the
9700X's lock with `taskset -c 2`, with `--directory` on each file system to
measure (the cache directory is under `$HOME`, btrfs there).
`docs/object-writer-and-linker.md` ("The runtime snapshot") has the numbers.
The default 9700X job runs it after the tests, pinned to one allowed CPU; like
the others, its measurements are informational.
