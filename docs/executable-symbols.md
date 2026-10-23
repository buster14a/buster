# Regular executable symbols

`ExecutableOptions.symbols` optionally attaches `.symtab` and `.strtab` to
native output independently of DWARF. This supplies the writer boundary needed
for the symbol-name and size comparisons in
[#86](https://github.com/buster14a/buster/issues/86). It does not itself select
foreign symbols, enable a command-line mode, or establish corpus equivalence.

## Producer and consumer contract

The caller passes an `ExecutableSymbols` descriptor: a borrowed, tightly packed
array of `count` 32-bit `SymbolId` values. IDs must be nonzero, valid in the
already checked `Link`, and strictly increasing. This permits a producer to
select symbols in its existing forward pass without another sort or a hash
set. The descriptor, IDs, names and definitions remain valid and unchanged
through `executable_write`. No padding is required. Empty selection is valid
and emits just the null symbol and empty string. The caller owns selection,
including omission of discarded or unwanted local symbols.

This attachment describes the named symbols retained by `Link`. The current
foreign-input representation does not retain original `STT_FILE` rows or the
distinction between an unnamed `STT_SECTION` row and other untyped rows. A
foreign producer must therefore define its selection as named semantic
symbols; this API cannot reproduce an input linker's complete bookkeeping
table. Corpus comparisons must separately check named definitions, sizes,
types, binding and visibility, and report this omitted metadata explicitly.

The writer validates bounded names and the total string-table capacity before
opening the destination. The vector path checks sixteen IDs per batch using
masked loads, including ordering across lane boundaries. Each lane validates
and counts a disjoint contiguous selection, publishing one cache line of
counts through the existing count-phase barrier. Emission uses the prefix of
those counts for disjoint name and local/global symbol ranges. It emits local
symbols before globals, retaining ID
order within each group, with the declared name, size, type, binding and
visibility. String bytes remain in the selection's original order. Definitions
use final atom placement: ordinary symbols have image addresses, TLS symbols
have module offsets, and IFUNC symbols have resolver addresses even when
references use a stub. Absolute definitions use `SHN_ABS`; undefined and
shared-library imports use `SHN_UNDEF` and value zero. Linker-defined image
boundaries remain associated with an allocated non-TLS section so a PIE's
boundary value remains relocatable.

A selected zero-sized TLS definition whose class has no output section is
currently refused before opening the destination. The writer must not pair
a TLS offset with a non-TLS section index. Other zero-sized definitions can
use the neighboring allocated section, as image-boundary symbols do.

The two tables are outside loaded segments. They participate in the normal
piece, hash, build-ID and output pipeline. They do not create dynamic exports.
With both symbol and debug attachments, the explicit symbol selection controls
`.symtab`; the debug descriptor still controls its own address slots. Without
an explicit selection, the debug descriptor retains its existing function
table and ordering. With neither attachment there is no symbol-table scan.

The debug descriptor currently retains its original serial name validation
and aggregate name-size limit even when an explicit symbol selection replaces
the debug-derived table. That conservative validation is unchanged in this
slice and remains part of measured coexistence cost.

## Validation and measurement boundary

`test-executable` includes independent ELF expectations for local ordering,
names, sizes, visibility, TLS offsets, IFUNC definitions and linker boundaries.
Guarded ID inputs exercise every length through 33 and every split point,
including duplicate, zero and out-of-range IDs. It compares complete output
across exact/vector writers and 1, 2, 3 and 8
lanes, recomputes the build ID, executes the original fixture, and checks that
invalid selections and missing TLS sections preserve an existing destination.
Existing `test-debug --cross-check` covers the debug-only attachment's consumers.
It also compares all five finalized DWARF sections with explicit empty and
single-function symbol selections across exact/vector writers and all four
lane counts, then restores the debug-only file for the external consumers.
Builds and tests run on the approved 9700X through the shared host lock.

The exact twin retains the scalar lane-zero baseline. The vector path's ID
classification and lane-partitioned validation/emission must be compared with
that baseline, including selection construction, validation, name copies,
output and cleanup, before making a performance claim or enabling foreign CLI
output. The variable-length name copies and individual ELF row packing remain
scalar within each lane; their costs need the same whole-path measurements.
Large/tiny symbol populations, mixed bindings, TLS, imports, debug coexistence
and frozen real workloads remain qualification gates.

The implementation and fixtures are original. ELF symbol fields and TLS
values are format/ABI facts already used by the repository's object and
dynamic writers; no external linker implementation or fixture is imported.
