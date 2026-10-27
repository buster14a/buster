# The C type table

`src/buster/compiler/c_types.c` is [#132](https://github.com/buster14a/buster/issues/132)'s
type table: every type of a translation unit as a `TypeId`, hash-consed so that
most compatibility checks are a compare, with the record layout, the
conversions, compatibility and composite types, and clang's spelling of a type
for diagnostics. It is contract 5 of
[the coordination comment on #135](https://github.com/buster14a/buster/issues/135#issuecomment-6293200394):
the checker (#133) and the calling convention (#137) build on its API, which the
top of the file documents.

The reference is clang 23 on x86-64 Linux (LP64, the System V psABI), as
[#154](https://github.com/buster14a/buster/issues/154) decided and
[#156](https://github.com/buster14a/buster/issues/156) requires: clang's
builtin types, its `getTypeInfo` sizes and alignments, its
`ItaniumRecordLayoutBuilder` layout, its `UsualArithmeticConversions`, its
`mergeTypes` for compatibility and composite types, and its `TypePrinter`.
clang's source was read for these rules; none of it is copied. `test-types`
checks each part against clang itself (see "Tests").

Nothing here waits for the parser: types come in through the API. `#pragma
pack` (#124), GNU's `packed` and `aligned` (#147) and member names (an
identifier id, #116's `IdentId` once #192 lands) are API inputs.

## TypeId

A `TypeId` is a `u32`: a type's index in the table, shifted left by 3, with
`const`, `volatile` and `restrict` in bits 0 to 2. Qualifying is an OR,
dropping qualifiers is a mask, and a qualified type has no record of its own.
Index 0 is none. `_Atomic` changes size and alignment, so it is a type of its
own, `ATOMIC`.

The table is columns, struct-of-arrays, pushed from one arena with the
capacities `type_initialize` is given: per type its kind, arithmetic code,
flags, alignment (log2), base, index, extent, size and key hash; functions'
parameters as ranges of one column; and per tag and per member columns of
their own. A request past a capacity fails and returns 0 with `last_error`
saying why, and so does every request clang refuses (`TypeError` lists them).

### Builtin types

The builtin types sit at fixed indices below 64, each equal to its arithmetic
code, so a builtin's `TypeId` is its code shifted left by 3 and a conversion
kernel's result is a `TypeId` after a shift. They are every builtin type clang
has on x86-64, as the owner asked on #132:

| Codes | Types |
| --- | --- |
| 1, 2 | `void`, `bool` |
| 3 to 5 | `char`, `signed char`, `unsigned char` |
| 6 to 13 | `short` to `unsigned long long`, signed then unsigned |
| 14, 15 | `__int128`, `unsigned __int128` |
| 16 to 22 | `__bf16`, `_Float16`, `__fp16`, `float`, `double`, `long double`, `__float128`: clang's `FloatingRank` order |
| 23 | `nullptr_t` |
| 32 + element | the complex types |

Plain `char` is signed, as x86-64 has it, unless the table's `char_unsigned`
says otherwise (`-funsigned-char`, which the checker's reader sets from the
unit's options): `type_integer_properties` answers its signedness, so the
conversions, promotions of bit-fields and machine modes follow it, and `char`
stays a type of its own either way. The promotions don't depend on it (every
character type promotes to `int`).

The floating codes in rank order make the usual arithmetic conversions of two
floating types a maximum. `_BitInt(N)` and complex `_BitInt`s are derived types
(hash-consed by N); their codes are `BIT_INT` (24), `UNSIGNED_BIT_INT` (25) and
32 plus those, codes but not indices. The builtin complex types are `_Complex`
of every integer type but `bool` and of every floating type but `__fp16`. clang
refuses to spell `_Complex __int128` and `_Complex __bf16`, but its usual
arithmetic conversions make them (`_Complex int` and `__int128` give
`_Complex __int128`), so they exist; `BUSTER_TYPE_COMPLEX_SPELLABLE` says which
a declaration may name.

### Derived types and their canonical forms

Pointers, arrays (of constant, unknown or variable size, `[*]` among the last),
functions, GNU `vector_size` and clang `ext_vector_type` vectors, `_Atomic`,
`_BitInt(N)`, complex `_BitInt`s and `ALIGNED` (a typedef's `aligned`
attribute) are hash-consed: equal structures get equal ids. Requests are
normalized first as clang's canonical types are, so that every spelling of a
type gives one id:

- an array's qualifiers go in the array's `TypeId`, and its base is the element
  without them (`const int[3]` and an array of `const int` are one id);
- a function's parameters are stored adjusted and unqualified (C 6.7.6.3p15:
  `void (const int)` is `void (int)`); its return type keeps its qualifiers,
  which clang keeps too; qualifying a function type drops the qualifiers;
- `ALIGNED` of `ALIGNED` is one `ALIGNED`, an alignment equal to a complete
  base's own is the base itself, and a vector's element loses its `ALIGNED`
  but keeps its qualifiers.

`ALIGNED` can lower an alignment, as clang's `__m128i_u` does. Its size, code
and completeness are its base's, read through the base, since the base may be a
tag completed later.

Sizes and validity follow clang's rules: an array's element must be complete,
its size a multiple of its alignment, and the array below 2^61 bytes;
`_Atomic` of a qualified, array, function, atomic, incomplete or `_BitInt`
type is refused, and up to 16 bytes an atomic type's size rounds up to a power
of two, which becomes its alignment; a `_BitInt(N)`'s alignment is N rounded up
to a power of two, from 8 to 64 bits, and its size N rounded up to that
(`_BitInt(128)` is 16 bytes aligned 8); a vector's alignment is its elements'
bits rounded up to a power of two, and its size that too, and the table refuses
vectors wider than 2^31 bits, where clang's 32-bit alignment arithmetic
overflows.

### GNU machine modes

`type_machine_mode(table, base, mode)` implements the target type action for
`__attribute__((mode(...)))`. It accepts bounded plain or paired-underscore
spellings, selects the supported scalar/complex target types and uses the
existing vector constructor for vector modes. It keeps the base's signedness
(plain `char`'s as `char_unsigned` says) and replaces the declaration's
type, stripping existing qualifiers and aligned typedef wrappers as Clang does;
later declaration qualifiers remain the checker's responsibility. Enum typedef
transformation leaves the original tag unchanged.

Unknown modes, incompatible bases, unsupported representations and oversized
vectors have explicit `TypeError` results. `test-types` checks types, canonical
spellings and surrounding record layouts against Clang, plus guarded names,
rejected cases and capacity failure. [The GNU extension guide](c-gnu-extensions.md)
records the supported modes and the still-missing source integration.

## Hash-consing

A derived type's key is its kind, its key flags (variadic, prototype,
unsigned), its base, its extent (an array's length, a vector's count, a
`_BitInt`'s width, an alignment's log2, or a variable array's size expression's
id) and, for a function, its parameters. The hash mixes three words,
`kind | flags << 8 | base << 32`, the extent and the parameters' hash, each by
an XOR and a multiply, and ends with SplitMix64's finalizer.

The index is open addressing over buckets of four 16-byte slots, one cache
line, probed bucket after bucket from the hash's. A slot holds as much of the
key as fits, the base, the extent's low 32 bits, the kind, the flags and 16
bits of the hash, so a lookup reads one line, and only a function's parameters
and an extent past 32 bits need the columns. The index lives in an arena of its
own and doubles when it is three quarters full, rebuilt from the hash column;
ids never change.

`type_derive_batch` takes requests as columns. It hashes eight keys per vector
(`vpmullq`), prefetches their buckets, and compares each bucket's four slots
with one vector compare. Misses go through `type_derive`, the one-at-a-time
path, in request order, so the ids never depend on the batch; so do functions,
extents past 32 bits and complex types of builtin elements. So does an
`ALIGNED` hit whose base is now complete with that alignment: the entry was
made while its tag was incomplete, and `type_derive` now gives the tag itself.
That check costs the benchmark's batch of hits 1.3 cycles a request (6% of its
requests are `ALIGNED`). Against
`type_derive` one at a time, on the 9700X:

| 65,536 requests | One at a time | Batch |
| --- | ---: | ---: |
| every one a hit | 82.9 cycles, 218.9 instructions | 22.7 cycles, 95.8 instructions |
| 1 in 8 a miss, into a fresh table | 105.2 cycles, 258.9 instructions | 60.4 cycles, 168.0 instructions |

The handoff proposed groups of 32 u16 tags compared with one `vpcmpeqw`; a tag
match then needs the columns to confirm it, a second line per lookup at least,
where a slot confirms the whole key in the line it is found in.

## Tags and layout

A tag (struct, union or enum) is one per declaration, never hash-consed. It
has its members (identifier id, type, bit-field width, explicit alignment,
packed, and the offset in bits that layout writes), its layout inputs (packed,
`#pragma pack`'s value and its `aligned` attribute) and its completion number,
the order in which tags are completed, from 1, for #113's deferred bodies. An
enum gets its underlying type when complete: fixed, or as clang chooses it from
the enumerators' values (`type_enum_value_bits` and `type_enum_underlying`,
`packed` included). `type_find_member` looks into anonymous members as C does,
through each record's lookup ("Member lookup" below), and returns 0 for no
member (member record 0 is reserved, as tag record 0 is), and
`type_builtin_va_list` makes x86-64's `__builtin_va_list`.

### Member lookup

Every `.` and `->` the checker reads finds its member by name (about 60,000 in
`sqlite3.c`), through anonymous members, as clang's lookup does: the first
member with the name in declaration order, depth first. Designated initializers
and `-Waddress-of-packed-member` look members up too, and before #591 the
lowering looked each access's member up again. The checker's lookup pushed and
cleared a byte per type of the table at every access (3,434 types in
`sqlite3.c`, about 200 MB of `memset` a compile), to guard against records that
hold one anonymous record more than once, and then compared the record's names
one at a time. Now each record's lookup is decided when its members are written
(`type_record_define`), and its cost depends on neither the table's size nor the
record's:

- **A scan**, for a record of at most 16 members (`BUSTER_TYPE_LOOKUP_SCAN`),
  none of them an anonymous struct or union: its names, contiguous in the
  members' column, compared with one masked 512-bit compare, which reads
  nothing past them.
- **An index**, for any other (`TypeLookup`): a hash table of every name the
  record's lookup finds, its anonymous members' records' names among them,
  each with the first of the record's own members, in declaration order, that
  finds it: the named member, or the anonymous member the name is in, whose
  record's lookup goes on from there. Buckets of 8 names and their members, a
  cache line each, compared at once; at most half full, so a probe reads one
  bucket, or a few. A lookup goes down one record per anonymous member on the
  way and never back, so records that hold one another many times cost it
  nothing (a union that holds the one before it twice, 64 levels deep, is a
  tree of 2^64 leaves).
- **A walk**, for a record whose index the table's buckets can't hold
  (`TypeCapacity.lookups`, one a member by default, which only anonymous
  members nested deeply outgrow), or one of whose anonymous members' records
  walks: depth first through the members, entering each record once, by a
  sparse set over type indices that nothing clears.

`type_lookup_member` takes the scan and the probe in vectors;
`type_lookup_member_scalar`, its twin, compares one name at a time over the
same indices, and the checker's scalar path takes it (`CheckAction.scalar`), so
that each comparison of the checker's two paths checks the kernels on every
member access of its corpus. The index is built once a record, scalar, one
insertion a name: it is the lookups that every access runs. The checker keeps
each access's offset in its stream (`CheckStream.member_offsets`), which the
lowering reads instead of looking the member up again.

### clang's layout

The layout is clang's `ItaniumRecordLayoutBuilder` for C on x86-64
(`LayoutField`, `LayoutBitField` and `FinishLayout`), in bits:

- **A member** aligns to its type's alignment raised by its explicit one, or
  to a byte raised by its explicit one when it or its record is packed; a pack
  caps both.
- **A bit-field** starts where the previous one left off. Packed lowers its
  alignment to a bit, the explicit alignment raises it, and a pack caps it
  (with packed too: its type's or explicit alignment, capped); neither packed
  nor a pack applies to a zero width. A zero width always moves to its
  alignment; another bit-field does only without a pack and when it would
  cross a unit of its type, and otherwise moves to its explicit alignment, if
  it has one within the pack. An unnamed bit-field doesn't raise the record's
  alignment.
- **A union's** members are all at 0, and a bit-field's data size is its width
  rounded up to bytes.
- **The record's** alignment starts at its `aligned` attribute, and its size
  rounds up to its alignment. A flexible array member takes its element's
  alignment and no size.

The scalar layout (`type_layout_scalar`) is this one record at a time, used by
`type_record_layout`, and the twin and fallback of the vector layout.

### The vector layout

`type_layout_records` lays out a batch of records whose members are all
complete (a caller completes nested records first, a level at a time) 16 at a
time, one record per 32-bit lane and a member of each per step, with masks for
the lanes whose records have no more members:

1. **The scalar pass** (`type_layout_read_types`) reads each member's type's
   size and alignment into two scratch columns, in the batch's order. It
   chases pointers (member, type, an aligned typedef's base), which on Zen 5
   costs less in scalar loads than in gathers of 64 µops.
2. **Grouping:** a counting sort by member count, so that a group's lanes end
   together.
3. **Blocks of 16 steps:** each lane's next 16 members are contiguous in the
   member and scratch columns, so masked loads read them, and a 16×16
   transposition in registers (64 shuffles) turns the lanes' rows into steps.
   Each step computes both a bit-field's and another member's placement and
   blends them by mask, as the scalar layout's branches choose; then the
   block's offsets are transposed back, and each lane's 16 are stored with
   masked stores.
4. **The fallback:** the arithmetic is in 32-bit bits, so a lane whose member
   is 2^27 bytes or more, whose alignments pass 2^26 bytes, or whose size
   reaches 2^31 bits is marked as the steps go and laid out by the scalar
   layout after its group.

The records are then completed in the batch's order. On the 9700X, the
benchmark's 4,096 records of 0 to 24 members of every kind (a third with
bit-fields), in 11 levels of nesting:

| Layout | ns/member | cycles/member | instructions/member | IPC |
| --- | ---: | ---: | ---: | ---: |
| one record at a time, as they were defined | 3.26 | 18.06 | 86.5 | 4.79 |
| scalar, the same levels | 3.39 | 18.77 | 86.5 | 4.61 |
| vector, the same levels | 2.66 | 14.76 | 49.3 | 3.34 |

The vector layout is 1.27 times the scalar one on the same batches and 1.22
times one record at a time in definition order, which reads the member columns
in order. Its time: the groups 48%, the scalar pass 33%, and the batch's sort
and completion 15% (`perf record`). The NSQ in front of the FP schedulers is
full for 12% of its cycles (`de_dispatch_stall_cycle_dynamic_tokens_part1.fp_sch_rsrc_stall`)
and the reorder buffer for 4%. With 512 records (6,125 members), which stay in
L2, the vector layout is 15.16 cycles a member and the scalar one 16.13:
much of the gain is in hiding the larger set's cache misses.

Two other designs lost, on the same benchmark:

- **Rows transposed by scalar stores:** each lane's members' inputs (size,
  alignments, width, flags) stored into rows of 16, which the steps then load.
  19.10 cycles and 81.0 instructions a member: per member, the transposition
  does as much scalar work as the scalar layout itself, and the steps' vector
  loads of rows just written by scalar stores fail store-to-load forwarding
  (`ls_bad_status2.stli_other`, where perf put most of its stalls on Zen 4).
- **Gathers:** each step gathering its members' and their types' columns, ten
  `vpgatherdd` a step. 17.50 cycles a member at an IPC of 1.73: the gathers'
  latency chain (member, type, kind, size) and their µops bound it.

## Conversions

The conversions are clang's: `type_promote` is the integer promotions and
`__fp16`'s promotion to float (`UsualUnaryConversions`), `type_promote_bit_field`
the same for a bit-field (`isPromotableBitField`: a `_BitInt` keeps its type, a
bit-field narrower than `int` becomes `int`, one as wide becomes `int` or
`unsigned int` as its type is signed or not, and a wider one promotes as its
type),
`type_default_argument_promote` adds `float`'s and `__fp16`'s to `double` and
leaves `_Float16`, `__bf16` and `nullptr_t` alone, and `type_usual_arithmetic`
gives the common type (`handleIntegerConversion` with clang's ranks, a
`_BitInt`'s by its width, complex integer elements unpromoted).

`type_usual_arithmetic_codes` does it on codes, 64 pairs a vector, with a
scalar twin:

1. both codes through a `vpermb` promotion table;
2. a class table says what each is: arithmetic, floating (real or complex),
   deferred (a `_BitInt` or a complex integer, which codes alone don't decide)
   and `_BitInt`;
3. two promoted integer codes, `int` to `unsigned __int128`, index a 64-entry
   table by their low 3 bits (`(left & 7) << 3 | (right & 7)`, one `vpermb`);
4. a floating pair takes the larger element, complex if either is, the OR of
   bit 5;
5. equal codes keep their code, and deferred pairs get `DEFERRED`, which
   `type_usual_arithmetic_types` gives to the scalar path. On TypeIds it reads
   builtins' codes from the ids themselves and other types' from the code
   column.

The brief offered `vpermi2b` on a pair of codes packed into one index; that
variant (`type_usual_arithmetic_codes_pairs`, a 128-entry table with the
floating ranks in its upper half) measured slower, so the 64-entry table is the
kernel. On the 9700X, 65,536 pairs drawn as a C program's operands are:

| Conversions | cycles/pair | instructions/pair |
| --- | ---: | ---: |
| scalar twin, on codes | 44.05 | 99.8 |
| `vpermb`, 64-entry table | 0.17 | 0.59 |
| `vpermi2b`, 128-entry table | 0.23 | 0.60 |
| `type_usual_arithmetic`, on TypeIds, one at a time | 44.20 | 115.0 |
| `type_usual_arithmetic_types`, on TypeIds | 2.25 | 5.38 |

The promotions are 1.61 cycles a code in the scalar twin and 0.04 with `vpermb`.

## Compatibility and composite types

`type_compatible` and `type_composite` are clang's `mergeTypes` in C17, on
explicit stacks in scratch memory. Types form a DAG: a pair of types can be
inside many others (`int (*)(T, T)`, nested, is n pairs of records but a tree of
2^n leaves), so each walk keeps a memo of the pairs it has met. Compatibility
pushes a pair once, and the composite takes a pair it has merged from the memo.
The canonical comparison (`type_same_canonical`, which looks through `ALIGNED`)
is a post-order walk whose memo keeps every pair's answer, and a composite
shares one such memo among all its comparisons, its frames' and its finishes':
two chains of n pointers that differ at the bottom would otherwise be compared
once per level, (n + 1)^2 pairs in all.

`type_compatible` and `type_same_canonical` answer `TYPE_ANSWER_YES` or
`TYPE_ANSWER_NO`, or `TYPE_ANSWER_FULL` when the scratch memory can't hold the
walk: no answer, which a caller must take for neither (a false "incompatible"
could pick another `_Generic` association). `type_composite` then returns 0
with `last_error` `TYPE_ERROR_FULL`, rather than choosing or making a type on a
missing answer, which could change an alignment.

Equal ids are compatible at once; otherwise:

- qualifiers must be equal, and `ALIGNED` is looked through, as clang's
  canonical types have no typedefs;
- an enum is compatible with its underlying type, compared as clang's
  `hasSameType` does, qualifiers included: `const enum E` and `const unsigned
  int` aren't, as clang has it;
- distinct tags never are (C23's tag compatibility is #133's);
- arrays are compatible when their elements are and their constant sizes
  agree; the composite takes the constant size, else the variable array (as it
  is, even where its element isn't the composite, as clang's FIXME has it),
  else the unknown size;
- a prototype against a function without one must have no `...` and no
  parameter the default argument promotions change, an enum's integer type
  counted;
- vectors, GNU's and clang's alike, are compatible when their elements and
  counts are equal (`areCompatVectorTypes`);
- a composite pointer or atomic type is made without the pair's own
  qualifiers, as clang's `getPointerType` and `getAtomicType` do there.

## Printing

`type_print` prints a type as clang's `TypePrinter` does, after a declarator's
name when given one. clang's printer recurses (`printBefore`, the name,
`printAfter`, with one flag, `HasEmptyPlaceHolder`, saved and restored around
parts); `type_print` runs the same steps from an explicit stack of frames. It
prints `_Bool` in C17 and `bool` with `BUSTER_TYPE_PRINT_C23`, as clang does in
each mode. Two things it can't spell as clang does: a variable array's size
expression (it prints `[]`, and `[*]` for `[*]`) and an anonymous tag's
location (it prints `(unnamed)` where clang names the file and line).

## Tests

`./build/compiler test-types [--seed N] [--count N] [--clang PATH] [--keep]`
(0.22 s on the 9700X):

- **Fixtures:** every builtin's kind, code, size and alignment; the requests
  clang refuses, each with its `TypeError`, and the edge cases it accepts; the
  spellings of a type that must get one id, and those that must not.
- **Hash-consing:** random constructions through every constructor, compared
  pair by pair with a structural comparison of the test's own, which writes the
  normalizations again from C's rules (4.5 million checks): equal structures
  get equal ids, and different ones never do.
- **Batches:** random requests, some invalid, given one at a time and in random
  slices of batches to two tables that must end up identical (results, columns
  and index), growing the index well past its first size; then `ALIGNED`
  requests of a tag completed since its wrapper was made, through both.
- **Layout:** clang's corner cases as fixtures, then 3,000 random records of
  every member type, bit-fields of every width next to every type, packed,
  packs, aligned, nesting, anonymous members and flexible array members, with
  member counts around the vector layout's blocks of 16 and inputs past its
  limits, laid out by the scalar layout in one table and by
  `type_layout_records` in another: identical offsets, sizes, alignments and
  completion order. Member lookup finds the first member of a fresh table,
  directly and through an anonymous member.
- **Member lookup** (`c_types_lookup_test.c`): the lookup, its twin, the walk
  and `type_find_member` against an oracle that lists a record's named members
  depth first through its anonymous members and takes the first with the name
  (member, offset and qualifiers): on random worlds of structs and unions of 0
  to 459 members (named ones, bit-fields named and not, members of record types,
  and anonymous members of earlier records, qualified, aligned and held by
  several), by every name and absent ones, through each record, qualified and
  aligned, with the default room for indices (no record may walk) and with room
  for 48 buckets (some must); the scan's limit (16 members, then 17, with and
  without an anonymous member), a name an anonymous member finds before a named
  member has it, and 24 names in one bucket of eight, whose probes go on and
  wrap around; a copy, a view taken back and a tag withdrawn and restored; the
  64 levels of unions of 2^64 leaves; one record of 100,000 members, every name
  found, through an index of no more buckets than its names ask for; the walk
  with every amount of room to 16 KB left in its scratch arena; and the scan's
  load at every count to 16 of names that end at a `PROT_NONE` page.
- **Conversions:** every pair of codes through both kernels and the scalar twin,
  at every length to 200 and ending at a `PROT_NONE` page; the TypeId batch
  against `type_usual_arithmetic` on enums, `_BitInt`s, aligned and atomic types;
  and fixtures of clang's results.
- **Compatibility, composite types and printing:** fixtures, the C standard's
  examples among them (C17 6.2.7p5, 6.7.6.2p6 and 6.7.6.3p21); types that
  share their insides 64 levels deep, whose trees have 2^64 leaves; composites
  of chains of 65,536 pointers over arrays of an aligned `int`; and the walks
  with every amount of room from none to 16 KB left in either scratch arena,
  which must answer right or say they couldn't.
- **clang as the oracle:** a random world of types, records and enums (1,766 by
  default; `--count` sets how many), written as C with a typedef per type built
  from earlier ones, so that the C spells each type independently of the
  printer. `clang -std=gnu17 -fsyntax-only` must accept its `_Static_assert`s of
  every size, alignment and member offset, of compatibility and of enums'
  underlying types (7,353); the same file compiled and run checks every
  bit-field's bits (643), since `offsetof` can't name one; and clang's AST dump
  gives its canonical spelling of every type, usual arithmetic conversion,
  promotion, bit-field promotion and composite type, which `type_print`'s must
  equal (6,189).

The files go in a new directory under `$TMPDIR` (or `/tmp`), removed unless
`--keep`. `--clang` picks the clang (clang 23 by default on `PATH`).

## Benchmark

```sh
EXCLUSIVE_OWNER=<name> ~/buster-fast-bench/exclusive.sh taskset -c 2 ./build/compiler benchmark-types
```

`benchmark-types [--seed N] [--requests N] [--records N] [--pairs N] [--only
construction|layout|conversions]` checks that each vector path agrees with its
twin, then reports the medians of 21 passes that alternate which path runs
first. The numbers above are its output on the 9700X (CPU 2, clang 23.1.1,
2026-10-20). Each vector path runs between scalar ones, as a compiler's vector
phases run after scalar work; #199 found that this state (the power cap's)
slows heavy AVX-512 loops on the 9700X.

## Boundary audit #298

Audit pin: `42630114aeb0553c3e2fabfd7fe9bc84b0ce6126`;
[scope and evidence](simd-boundary-audit.md).
[The type-table map and ownership](https://github.com/buster14a/buster/blob/42630114aeb0553c3e2fabfd7fe9bc84b0ce6126/src/buster/compiler/c_types.c#L4)
cover these implemented substages and shared consumers:

| Producer → consumer | Reusable information and ordering |
| --- | --- |
| Checker declarators/actions → `type_derive` / `type_derive_batch` | Normalized kind/flags/base/extent/parameter keys; hash-consed IDs. Batch misses are inserted in request order, preserving IDs across partitions. |
| Tag/member producer → `type_record_define` / `type_layout_records` | Member/type/attribute data and explicit completion; layout publishes offsets, size and alignment for checker, static-data and ABI readers. A dependent member must be complete before its record's layout. |
| Type columns → arithmetic conversions / compatibility / composite walks | Fixed arithmetic codes permit byte-table vector lookup; tagged or exceptional types use exact actions. Pair memos avoid repeated traversal of shared type DAGs. |
| Type table → ABI classification, checker/builtin adapters, diagnostic `type_print` | TypeId remains a u32 `(index << 3) | qualifiers`, zero none; IDs are table-local. The formatter is a cold reader, not a prerequisite for semantic operations. |

Columns use their declared widths and conventional C strides; capacity and
logical type/member/parameter counts differ. `type_initialize` allocates fixed
columns in the supplied arena, with a separate growing index arena. Capacity
or invalid derivation returns zero with `last_error`, never a truncated ID.
Requests/results and temporary layout work must not overwrite live input keys.
The vector batch/layout kernels bound tails; their internal eight-key and
16-record widths do not authorize reading beyond a caller's count. The type
table and member/parameter/name storage must survive all consumers.

There is one writer. Most columns become immutable after publication, except
completion of a tag. Parallel readers require no concurrent mutation; a cached
ABI row or deferred body environment cannot silently observe a later completion
as though it existed earlier. [#113](https://github.com/buster14a/buster/issues/113)/[#133](https://github.com/buster14a/buster/issues/133) own historical semantic contexts.

Observed duplication candidates are dependent-type readiness scans in ABI and
one-at-a-time type requests from source elaboration despite a batch API. A
producer-supplied ready frontier or completion list could help, but emitting
unused levels for every type could cost more; keep dependencies and canonical
ID order. The existing bulk/small-batch `benchmark-types` and ABI benchmarks
measure modules, not a source unit's whole handoff. Disposition: retain canonical
columns and exact twins. `test-types` checks legal batch partitions, layout,
compatibility and Clang rules; [#133](https://github.com/buster14a/buster/issues/133)/[#137](https://github.com/buster14a/buster/issues/137) require actual source request streams,
small/large combined costs, memory and conditioned occupancy before changing
these interfaces.
