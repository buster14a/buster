# Deterministic output

The output order belongs to the input, not to a lane or an intern table.
Issue [#153](https://github.com/buster14a/buster/issues/153) applies this rule
to diagnostics, preprocessed text, literal pools, objects and executables.

## Diagnostic groups

`src/buster/compiler/diagnostic.c` supplies bounded lane buffers. Before a
phase starts, its caller assigns each job its translation-unit ordinal and
its unique source-order job ordinal within that unit. A job has one writer
and its own diagnostic sequence counter; jobs may move between lanes before
they start.

`diagnostic_append` copies a rendered group into the lane's text arena. A
group contains a primary diagnostic and all of its notes, including include
and macro-expansion notes. Its display text is independent of the ordering
key. Rendering while the job still owns its source and arguments avoids
retaining a `va_list`, pointers into temporary argument buffers, or a mutable
preprocessor just to print later. The severity is retained separately.

After all jobs finish, `diagnostic_merge` orders the groups by:

1. Translation-unit ordinal, in command-line input order.
2. Canonical source position within that unit.
3. Job ordinal, for jobs reporting at the same position.
4. Sequence within that job.

Canonical positions must be assigned from the source traversal before
scheduling. A presumed line number from `#line`, a file-cache record number,
an intern ID and the number of a completing job are not source positions.
Notes stay attached to the primary, even if they describe an earlier location.

Two 64-bit keys pack the four full-width 32-bit fields without truncation.
The stable LSD radix sort runs eight byte passes on each key. It takes 16
digits at a time: AVX-512 conflict masks and population counts give each
duplicate digit its rank, and the last copy advances the bucket count once.
Placement uses those ranks and scalar stores, avoiding gathers and scatters.
Inactive tail lanes cannot enter the histogram. Counts up to 256 use insertion
sort, which is also the exact scalar oracle.

A fatal stops its own job. Further appends from that job return false without
marking the buffer as failed. It does not cancel another job on whichever lane
happens to observe it first. The merge retains the canonical prefix through
the earliest fatal group, including its notes. Earlier jobs must finish;
later results may be discarded. Resource failures are separate: a full record
buffer, exhausted text arena, sequence overflow or failed merge allocation
rejects the whole result before printing. A caller requiring reproducible
resource-limit diagnostics must budget by job, not by whichever jobs share a
lane. `diagnostic_write` reports write failures; the caller owns stream flush
and close errors.

The caller owns record storage and arenas. Each lane writes only its own
buffer. The merger runs after the workers join and uses a separate arena;
both the lane text and the merged records remain alive through printing.

## IDs and placed output

`IdentId` and `NameId` identify spellings. They do not set emission order.
The intern table reserves fixed IDs for keywords and directive/builtin names;
the property tests preserve this API contract and change every other name's
assignment by priming the table with unused names and a reordered vocabulary.

The link representation's atom, symbol and fixup arrays are already placed
input ranges. Their indices express that canonical placement, rather than
an arbitrary intern insertion order. Tests permute `NameId`, update all
symbol-name references, rebuild preparation that caches those IDs, and preserve
those ranges. The object tests freeze the reference bytes before any
permutation, for both section modes.

Future literal pooling must first find each value's earliest source use and
place distinct values by that position, with an explicit source-order tie
break. Content equality and hashes can deduplicate; they cannot determine
placement. Jobs producing symbols, sections and relocations likewise count,
place and fill canonical ranges before those records reach the writer. The
existing generated-program tests cover string-pool bytes and their fixups;
they do not stand in for an unimplemented C literal-pooling frontend.

Foreign resolution keeps hash-partition name IDs for lookup. Its separate
publication permutation orders each name by its first ordinary object symbol:
round, then item in that round's symbol columns (objects in input order and
symbols in symbol-table order). Command-line `-u` records precede the objects.
A name seen only in shared-library references follows all ordinary object
records, in its own first-record order. Unloaded archive index entries do not
publish names or choose output positions.

The winners pass retains the minimum key while it reads each record. The
already checked cumulative global-symbol ordinal represents `(round, item)`
without truncation; ordinary and library-only keys form separate ranges, so
the class does not consume an ordinal bit. A four-byte stable radix sort of
each range has exact scalar and vector paths. The resulting immutable
permutation lives with `Foreign`; temporary sorting storage is released before
conversion, and an allocation failure rejects the resolution.

Scalar conversion walks that permutation. Lane conversion counts and fills
consecutive ranges of it, retaining hash IDs only for maps and translating a
failure position back to its name. Common atoms, copy-relocation groups and
representatives, linker-generated section-bound groups, and executable/shared
library export lists likewise follow canonical order. This includes placement
that occurs after the initial global symbols have been created.

## Property tests

- `test-diagnostics`: radix against stable insertion sort on counts 0–319
  and 4,095–4,097, every tail ending against a guard page, repeated keys,
  forward/reverse/random orders and high key bits. Lane tests compare rendered
  groups at 1, 2, 3 and 8 lanes with reversed jobs and delayed completions,
  through both sorting paths. Limits include empty input and record, text and
  merge exhaustion.
- `test-preprocessor`: every built-in fixture is also an independent lane
  job. Both kernel paths, both job orders and 1/2/3/8 lanes must reproduce a
  fixed baseline's `-E` bytes, with and without markers, diagnostic bytes,
  status, warnings, errors and fatal state after reseeding the intern table.
  The test checks that the IDs actually change. This harness uses the callable
  preprocessor; it is not a production multi-file driver or a shared cache.
- `test-object-writer`: both section modes at every lane count, with permuted
  names, compared with bytes captured before permutation, including an empty
  trailing unit.
- `test-executable`, `test-dynamic`, `test-static`: permuted names between
  writes at different lane counts, compared with the original-ID executable
  and build ID. Existing execution, ELF structure and independent linker
  checks still apply, including static PIEs.
- `test-files`: #150's shared file cache, filled on lanes from a walk. Every
  check runs again on 1/2/3/8 lanes with the shared table primed before the
  walk, with unused names and every other name of a first run's table in
  another order, so that the merge finds those and numbers the rest after them.
  Every translation unit's `-E` bytes, with and without markers, diagnostic
  bytes, errors, warnings and stream locations must equal the first run's, each
  ID in its stream and in every cached file's tokens spelled the same. The
  first run's names must all be there, and both those primed and those the walk
  adds must have changed IDs. This covers the tree, the generated spellings,
  the contexts and special paths, and in the workflow the repository's units
  and `--flat src`.
- `test-foreign`: dedicated `foreign_order_test.c` coverage varies
  `partition_records` and adds thousands of unused archive-index symbols.
  Original C objects, an assembly entry and a DSO exercise scalar/vector
  conversion at 1/2/3/8 lanes. Complete static/PIE bytes, `.symtab`, `.dynsym`,
  GOT slots and execution must agree. Independent symbol-table walks require
  first-record order, and the real driver repeats unused-index/lane changes
  without an environment override. Guarded radix tails, high ordinal bits,
  unsigned IDs above `INT32_MAX` and allocation refusals cover the ordering
  kernel's bounds.

The four output test commands, `test-preprocessor` and `test-files` accept
`--id-order N` (default 153) to choose another permutation, independently of
fixture seeds. The default 9700X workflow runs these property checks in their
stage commands and runs `test-diagnostics` explicitly.

`benchmark-diagnostics [--only scalar|selected] [--after-scalar MS]` compares insertion sort with the
selected path on the same frozen random keys, including the input copy. It
prints medians of 21 interleaved samples after one warm-up, in nanoseconds and
invariant-TSC ticks per key. TSC ticks are not hardware core-cycle counts.
Run it on CPU 2 under the shared lock. The default interleaves the two paths;
`--only selected` runs the selected path back to back, with longer batches for
the large radix sort. `--only selected --after-scalar 100` conditions each
short timed batch with 100 ms of lighter insertion sorts. Repeat each in
several processes: an interleaved run alone does not establish either state.

## 9700X measurements

Measured on 2026-10-22 with Clang 23.1.1, `build.sh`'s `-O3 -march=native`,
CPU 2 and the shared exclusive lock. Inputs are the benchmark's frozen seed
153 keys; copying the input is included. Each number below is the range of
21-sample medians from three fresh processes, in nanoseconds per key.

The cutoff experiment at `b1b1168fd4fe5d058b6c01ded14a152aee1b989e`
selected radix above 32 records, allowing both algorithms to be measured at
64 and 256. The two paths alternate within each process. Conditioning, when
enabled, precedes each timed sample.

| Records | Insertion, default | Radix, default | Insertion, after 100 ms scalar | Radix, after 100 ms scalar |
| ---: | ---: | ---: | ---: | ---: |
| 64 | 8.93–8.97 | 51.81–53.61 | 8.97–8.98 | 51.98–53.71 |
| 256 | 43.59–43.74 | 43.41–44.14 | 43.02–43.83 | 43.61–44.21 |
| 4,096 | 614.48–614.67 | 43.26–43.44 | 614.84–616.10 | 43.34–43.37 |

Insertion clearly wins at 64, the paths overlap at 256, and radix wins at
4,096. The selected path therefore uses insertion through 256. These are
sorting measurements on random keys, not whole-compiler throughput claims
or a claim that radix wins on every already ordered input.

## Integration boundary

The property tests above cover the currently callable preprocessor and
output writers, and `diagnostic.c` supplies the diagnostic buffer API. #153's
independent dependencies each add their own integration check:

- #150's shared file cache must exercise its own `test-files` output with
  changed intern assignments, preserving the same spellings and locations.
  Done: `test-files` runs every check again with the shared table primed
  before its walk ("Property tests").
- #151's per-function frontend/backend must assign canonical job positions,
  literal first uses and output ranges, then compare actual compilation
  results at every lane count. Done for the backend's emission
  (`check_program_emit_lanes`, [the lanes guide](function-lanes.md)): its
  parts are consecutive bodies in source order, the runtime's functions and
  the label symbols each part numbers are numbered in source order after the
  lanes, the link's atoms, symbols and fixups are added in source order, and
  the failure reported is source order's. `test-check-lanes` compares objects,
  `-g`'s rows, unwind tables, executables and failures at 1, 2, 3 and 8
  lanes, with a slow lane, against one lane's.
- #152's multi-file driver must route jobs' diagnostic groups through the
  merge, finish the earlier jobs before applying fatal cutoff, and compare
  its final objects, executable, `-E` text and diagnostic stream.
- #86/#594's foreign-input conversion has its own first-record publication
  order and partition/ID perturbation property checks in `test-foreign`
  ([#734](https://github.com/buster14a/buster/issues/734)). Hash partitioning
  and lookup IDs do not determine emitted globals, GOT slots or export order.

The others' integration checks belong in each stage's test command as it lands.
Passing the current tests is not proof of those future pipelines, and #153
must retain that distinction when its overall completion is assessed.

## Boundary audit #298

Audit pin: `42630114aeb0553c3e2fabfd7fe9bc84b0ce6126`;
[scope and evidence](simd-boundary-audit.md). Shared side consumers include
[diagnostics](https://github.com/buster14a/buster/blob/42630114aeb0553c3e2fabfd7fe9bc84b0ce6126/src/buster/compiler/diagnostic.c#L1),
[per-file publication](https://github.com/buster14a/buster/blob/42630114aeb0553c3e2fabfd7fe9bc84b0ce6126/src/buster/compiler/c_files.c#L4)
and [ordered output tests](https://github.com/buster14a/buster/blob/42630114aeb0553c3e2fabfd7fe9bc84b0ce6126/src/buster/compiler/link_order_test.c#L1).
They are actual constraints on every attempted regrouping, not a final sort
that can repair arbitrary semantic reordering.

Per-file preparation publishes global intern IDs in file/first-occurrence
order after count/place/fill and remap. PP history preserves distinct expanded
occurrences even when spellings/locations are reused. Parser source indices and
checker report sorting preserve diagnostic order through grouping. Native output
preserves source/input identity, symbol/atom order and required init/fini order
while lanes complete at different times. File-cache and output tests vary
1/2/3/8 lanes and delayed completion; a new handoff must carry its identity and
ownership metadata through any permutation.

The diagnostic module's actual edges are rendered groups → `diagnostic_append`
→ lane buffers → `diagnostic_merge` → `diagnostic_write`. Append copies the
primary and notes together into a lane-owned text arena. Records retain two
u64 keys packing four u32 fields (unit, source location, job, sequence), a text
slice and severity; counts/capacities are explicit. The caller joins all jobs
before merge sorts bounded order records using separate scratch: insertion sort through 256
records, otherwise 16-key radix work with initialized local tails. It retains
the prefix through the first fatal group. Any append/merge failure rejects the
whole result before printing. A source job has exactly one writer.

PP/cache/spelling storage must survive rendering; after append copies rendered
text, that diagnostic no longer borrows those tables. Lane text, merged records
and their order array survive through `diagnostic_write`. Code/data bytes
remain alive through patch/hash/write. Borrowed data
cannot be reclaimed merely because its producing lane returned. Publication
requires all participating lanes to agree on success. Per-lane scratch and
output ranges are disjoint; barrier state is not permission for two lanes to
mutate the same append-only table. Count/overflow/failure handling stays part of
the producing stage's contract.

Disposition: retain explicit identity and publication rules. `test-diagnostics`
checks this standalone buffer/merge API; it is not a connected multi-unit source
driver at the pin. No new global sorting/batching layer is proposed. [#151](https://github.com/buster14a/buster/issues/151)/[#152](https://github.com/buster14a/buster/issues/152)/[#153](https://github.com/buster14a/buster/issues/153) retain the complete
per-function/multi-unit source driver, ordered diagnostics and final binary
comparison. Existing generated-link/per-file schedule tests establish their
own paths only; they do not measure absent source compilation, nor justify
charging synchronization or output reordering outside a future benchmark.
