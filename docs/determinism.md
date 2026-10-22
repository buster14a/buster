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
  results at every lane count.
- #152's multi-file driver must route jobs' diagnostic groups through the
  merge, finish the earlier jobs before applying fatal cutoff, and compare
  its final objects, executable, `-E` text and diagnostic stream.

The others' integration checks belong in each stage's test command as it lands.
Passing the current tests is not proof of those future pipelines, and #153
must retain that distinction when its overall completion is assessed.
