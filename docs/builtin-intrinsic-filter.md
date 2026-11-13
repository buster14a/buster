# Intrinsic catalog negative filter

The vector path of `builtin_intrinsic_lookup_batch` rejects definite misses
before the existing exact binary search (#893). The public scalar lookup
deliberately remains unfiltered: it is the independent semantic oracle.
No catalog row, name, descriptor, scope rule or capability changes.

## Producer, handoff and consumer

`tools/generate_builtin_intrinsic_filter.c` projects every name in the checked-in
`generated/clang_builtin_metadata_23_1_1.inc` into a 65,536-bit immutable bitmap.
There is no runtime initialization, allocation or per-translation-unit pass.
The producer reads the existing reviewed LLVM metadata, not a new installation
or a name-prefix heuristic. Plain libc names are included too.

`builtin_intrinsic_filter_key` samples the first and last four logical bytes
and the length. Lengths below four use only their available bytes. This is
intentionally a bounded-cost fingerprint, not a full-name identity or a
cryptographic hash. Middle-only changes can collide by construction. The
eight-lane hash mixing has an exact scalar twin; scalar bit probes avoid
hardware gathers. The consumer seeds the existing binary-search intervals
only for positive bits. Every positive still needs an exact bounded comparison.

Input records and output records retain the catalog's disjoint counted-slice
contract. The internal candidate kernel accepts zero to eight records; zero
permits null. Null or empty names miss, inactive records are never read, and
names need neither NUL termination nor readable padding. No new mutable state
or lifetime crosses the handoff. The filter occupies 8 KiB of read-only data.

## The set-up's memo of each name's answer

After the filter, `check_input_intrinsic_initialize`'s cost was the walks
around the search rather than the search. Each unit:

- cleared a 4-byte cache as long as the lane's intern table, which grows with
  a command's units;
- tested every operand again by scalar loads, then read and wrote its name's
  cache entry;
- looked every distinct operand name up again, though the answer is the
  spelling's alone;
- walked the operands a second time to count the calls.

The intern table now keeps each id's answer (`InternTable.intrinsic_rows`, a
16-bit code: not looked up, no row, or the row plus 2). It lives in its own
fresh reservation, made at its first use and made again larger when the table
has outgrown it (`intern_intrinsic_rows`); `intern_reset` and
`intern_release` drop it. The units of a command that share a table look each
spelling up once.

- **One pass.** The set-up reads each operand's code once. It counts the
  distinct rows (a bit per row) and the call-shaped operands of rows together,
  which size the tables as before.
- **The vector path** takes the 64-token mask's operands without testing each
  again. It queues a name not yet looked up for the eight-lane filter and
  search, marking it pending, and the operands that meet a pending name wait
  for it: at most 8 names and 64 operands, after which the batch is looked up
  and its waiting operands are counted.
- **The scalar path**, its twin, tests each token and looks each name up by
  the unfiltered exact search. Both give every name its exact code and the
  same counts, and no code is left pending.
- **Storage.** Only a unit with a row's name allocates: the tables, and the
  name cache, made from the memo, 32 codes at once on the vector path. A name
  the memo hasn't looked up keeps `UINT32_MAX`, which its readers look up
  themselves, as before. The other names' entries are the exact answers the
  scan would have written.

`test-checker`'s intrinsic memo test runs synthetic units on both paths
against an oracle that looks every operand up by the exact search. The units
mix operands, calls, other identifiers, deferred tokens, rows and their near
misses. They run on fresh tables, and on shared tables that grow past their
first room between units. One unit makes more than 64 operands wait for a
batch of two names. Every run must give the oracle's counts, leave each code
exact and none pending, and give each cache entry its exact answer or
`UINT32_MAX`. A reset table must drop its codes.

## Reproduction and regression coverage

Compile and regenerate from the repository root (replace the checked-in include
only after the generator exits successfully):

```sh
clang -std=c17 -O2 -Wall -Wextra -Werror tools/generate_builtin_intrinsic_filter.c -o /tmp/buster-intrinsic-filter
/tmp/buster-intrinsic-filter > /tmp/buster-intrinsic-filter.inc
cmp /tmp/buster-intrinsic-filter.inc src/buster/compiler/generated/clang_builtin_intrinsic_filter_23_1_1.inc
```

The generated row-count assertion detects changed catalog cardinality. The
existing `test-builtin-intrinsic-catalog` command additionally reconstructs
the whole bitmap, checks every hit against its original row identity, forces
fingerprint collisions through the exact search, compares mixed batches with
the unfiltered scalar lookup, varies all one-to-eight-lane tails, and places
short, empty and null slices beside protected pages. The existing catalog
partition/input/output guard tests also exercise the integrated filter.

## Provenance and acceptance

The generator, fingerprint kernels and tests are original project code under
Apache-2.0 WITH LLVM-exception. The generated bitmap is a projection of the
existing LLVM name facts and retains their separately recorded provenance and
license, not an assertion of first-party ownership. The initial input is the
metadata blob `1d5cb30be9261cadb98ab80fc288de1749ea3d2d` at repository commit
`e693d878309ed3c3be05f2f550574d59217deceb`; the underlying package hashes and
llvmorg-23.1.1 references remain in
`docs/builtin-metadata/clang-23.1.1-inputs.json` and `LICENSES/LLVM.txt`.

Correct rejection is not a throughput claim. Acceptance still needs locked
9700X assembly inspection and A/B whole-compilation and intrinsic-setup
measurements on SQLite and small inputs, including filter loads, hashing,
collisions and remaining binary searches. Compare scalar and vector output,
retain generated-program quality, and store raw performance evidence in
buster-performance rather than this repository. The original issue remains
open until that acceptance is established.
