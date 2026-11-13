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
fresh reservation (`intern_intrinsic_rows`), with 32 codes of zero padding for
the gathers' 32-bit reads:

- **Its first size is 8 KB**, which a small unit's names fit. It doubles when
  the table outgrows it, copying the codes.
- **Up to 32,736 ids it stays in base pages**, the arena's first 64 KB
  (sqlite3.c and its headers intern 22,800). There a reservation takes no
  huge-page advice and splits no mapping.
- **`intern_reset` and `intern_release` drop it.**

The units of a command that share a table look each spelling up once.

- **The fill (#2654).** Before the operand pass, `check_input_intrinsic_fill`
  gives every id from the table's watermark (`InternTable.intrinsic_known`) to
  its count a code, in id order, which is the order of the records.
  - **The vector path** reads 16 spans at once, gathers each spelling's first
    and last four bytes, and computes the filter's key, hash and bit in the
    lanes. A spelling of under four bytes is read from its first byte and
    masked to its length where four bytes from it lie within the written
    records; the table's last such records take the scalar test.
  - **The scalar path**, its twin, tests each id
    (`check_input_intrinsic_candidate`).
  - **Candidates** go to the exact table, and every other id is a miss's.
  - **Later units** of a command fill only the ids that came since.
- **The exact table** (`builtin_intrinsic_table`, `c_builtin_intrinsic_catalog.c`)
  holds each row in one of 8,192 slots: its row plus one, and a 16-bit tag of a
  hash of the whole name. A lookup hashes the name once and compares the names of
  the rows whose tag matches, where the binary search made about twelve
  compares. It is baked into the executable (`bake-tables`); an executable that
  isn't baked makes it at first use, before a command's lanes start. The binary
  search stays its oracle.
- **One pass over the operands.** The set-up reads each operand's code once. It
  counts the distinct rows (a bit per row) and the call-shaped operands of rows
  together, which size the tables as before.
  - **The vector path** takes the 64-token mask's operands without testing
    each again, and gathers their names' codes 16 at once
    (`check_input_intrinsic_unresolved`). Only a row's go on, to an out-of-line
    handler (`check_input_intrinsic_take`).
  - **The scalar path**, its twin, tests each token. Both give every name its
    exact code and the same counts.
- **Storage.** Only a unit with a row's name allocates: the tables, and the
  name cache, made from the memo, 32 codes at once on the vector path. Every id
  the table has when the set-up runs has its exact answer there; a later one
  keeps `UINT32_MAX`, which its readers look up themselves, as before.

The tests:

- **The memo test** (`test-checker`) runs synthetic units on both paths
  against an oracle that looks every operand up by the exact search.
  - **The units** mix operands, calls, other identifiers, deferred tokens, rows
    and their near misses.
  - **The tables** are fresh ones, and shared ones that grow past their first
    room between units.
  - **Every run** must give the oracle's counts, leave each code exact, and
    give each cache entry its exact answer or `UINT32_MAX`. A reset table must
    drop its codes.
- **The fill test** (`test-checker`) builds tables of every catalogue name, a
  near miss of each, and short names, the last of them under four bytes.
  - **The 16-lane filter** must pass exactly the ids its scalar twin passes.
  - **Both paths**, filled in one step and in two, must give every id the
    binary search's code.
- **`test-builtin-intrinsic-catalog`** checks the exact table against the
  binary search:
  - every row, and each row's near misses;
  - library, short and empty names, and the bounded slices;
  - every name ending at a guard page;
  - a second build gives the same slots.

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

Correct rejection is not a throughput claim. The filter's locked 9700X
measurements are buster-performance's #146 and #164 (#914), and the memo's are
in `docs/compile-time.md` ("The intrinsics' set-up from a memo of each name's
answer"), each with its raw evidence in buster-performance. A unit whose names
the table has never seen still pays the filter's key loads from its
spellings and its false positives' binary searches.
