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
