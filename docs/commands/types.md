# types

Run commands and resolve source paths from the repository root. Follow the
[shared agent rules](../../AGENTS.md), including the 9700X lock and `cores`
requirements. [Command index](README.md).

## Module overview

- `src/buster/compiler/c_types.c` is the C type table (#132, contract 5 for
  #133 and #137): `TypeId`s, hash-consed one at a time and in batches, tags,
  clang's record layout (one record at a time, and 16 at once in vectors), each
  record's member lookup (#591: a scan of its names, or an index of the names
  its anonymous members hold too, built when it is defined, with
  `type_lookup_member_scalar` its twin and a walk where an index doesn't fit),
  the conversions as `vpermb` tables 64 pairs at a time, compatibility and
  composite types, and clang's spelling of a type, all as clang 23 has them on
  x86-64. `c_types_test.c` holds its tests (`main.c` includes both;
  `./build/compiler test-types` runs them) and `c_types_benchmark.c` provides
  `benchmark-types`. `docs/c-types.md` records the design and its 9700X
  measurements.
## Type tests

`./build/compiler test-types [--seed N] [--count N] [--clang PATH] [--keep]`
(about 0.2 s on the 9700X) checks `c_types.c`. It needs clang 23 on `PATH`, or
the one `--clang` names:

- builtins: every builtin type's kind, code, size and alignment, from a table
  of clang 23's values of its own;
- validation: each request clang refuses, with the `TypeError` it gets, and the
  edge cases it accepts; and normalization: the spellings of one type that must
  get one `TypeId` (a qualified array and an array of qualified elements,
  qualified parameters, an aligned typedef of one, or of its type's own
  alignment), and those that must not;
- hash-consing: random constructions through every constructor, compared pair
  by pair with a structural comparison of the test's own, which writes the
  normalizations again from C's rules: equal structures get equal ids, and
  different ones never do;
- batches: random requests, some invalid, given one at a time and in random
  slices of `type_derive_batch` to two tables, which must end up identical
  (results, columns and index), with the index grown well past its first size;
  then aligned typedefs' requests of a tag completed since their wrapper was
  made, through both;
- layout: clang's corner cases, then random records of every member type,
  bit-fields of every width next to every type, packed, packs, aligned,
  nesting, anonymous members and flexible array members, with member counts
  around the vector layout's blocks of 16 and inputs past its limits, laid out
  by the scalar layout in one table and by `type_layout_records` in another:
  identical offsets, sizes, alignments and completion order; and member lookup
  of a fresh table's first member, directly and through an anonymous member;
- member lookup (#591, `c_types_lookup_test.c`): the lookup, its scalar twin,
  the walk and `type_find_member` against an oracle that lists a record's
  named members depth first through its anonymous members (member, offset and
  qualifiers), on random worlds of records of 0 to 459 members that hold one
  another as anonymous members, qualified and aligned, by every name and
  absent ones, with the default room for indices and with room for 48 buckets,
  past which records walk; the scan's limit of 16 members, a name that an
  anonymous member finds before a named member has it, and 24 names in one
  bucket of eight; a copy, a view taken back and a tag withdrawn and restored;
  unions that hold the one before them twice, 64 levels deep; a record of
  100,000 members; the walk with every amount of room to 16 KB left in its
  scratch arena; and the scan at every count to 16 of names ending at a
  `PROT_NONE` page;
- conversions: every pair of codes through both vector kernels and the scalar
  twin, at every length to 200 and ending at a `PROT_NONE` page; the TypeId
  batch against `type_usual_arithmetic`; and clang's results as fixtures;
- compatibility, composite types and printing: fixtures, the C standard's
  examples among them, types that share their insides 64 levels deep (2^64
  leaves), which each walk must take pair by distinct pair, composites of
  chains of 65,536 pointers, whose canonical comparisons must share one memo,
  and the walks with every amount of room to 16 KB left in either scratch
  arena, which must answer right or say they couldn't;
- clang as the oracle: a world of random types, records and enums (`--count`
  sets its size), written as C with a typedef per type built from earlier
  ones. `clang -std=gnu17 -fsyntax-only` must accept its `_Static_assert`s of
  every size, alignment and member offset, of compatibility and of enums'
  underlying types; the same file compiled and run checks every bit-field's
  bits; and clang's AST dump gives its spelling of every type, usual
  arithmetic conversion, promotion, bit-field promotion and composite type,
  which `type_print`'s must equal.

The files go in a new directory under `$TMPDIR` (or `/tmp`), removed unless
`--keep`. `--seed N` changes every random input. The 9700X job's test step
runs it.

## Type benchmark

`./build/compiler benchmark-types [--seed N] [--requests N] [--records N]
[--pairs N] [--lookups N] [--only construction|layout|conversions|lookups]`
checks that each vector path agrees with its scalar twin, then reports 21
interleaved-pass medians per item (time, cycles, instructions, IPC):

- construction: 65,536 derivation requests over a working set of derived
  types, every one a hit and then 1 in 8 a miss into a fresh table, one at a
  time (`type_derive`) and as a batch (`type_derive_batch`);
- layout: 4,096 records of 0 to 24 members laid out one at a time as they were
  defined, and in levels of nesting by the scalar layout and by the vector one
  (`type_layout_records`);
- conversions: 65,536 pairs of arithmetic codes, drawn as a C program's
  operands are, through the scalar twin and the `vpermb` and `vpermi2b`
  kernels; the promotions; and the same pairs as TypeIds through
  `type_usual_arithmetic_types` and `type_usual_arithmetic`;
- lookups (#591): 65,536 member accesses, skewed toward a few, of 4,096 records
  shaped as a unit has them (most scanned, some indexed, one in 10 with an
  anonymous member), by `type_lookup_member`, its scalar twin and the walk; and
  every record's lookup built again, per member.

Run it under the 9700X's lock with `taskset -c 2`. `docs/c-types.md` has the
numbers and the layout designs the vector one was chosen over. The default
9700X job runs it after the tests, pinned to one allowed CPU; like the
others, its measurements are informational.
