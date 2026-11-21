# Foreign string merging

`foreign_strings.c` interns complete zero-terminated entries from read-only
ELF sections marked `SHF_MERGE | SHF_STRINGS`. One-, two- and four-byte code
units are supported. A zero byte inside a nonzero wide code unit does not
terminate an entry. Other entry widths retain their unmerged storage.

This is a bounded part of [#86](https://github.com/buster14a/buster/issues/86).
The offset-map producer and Link consumer are separate so their correctness,
storage and whole-pipeline costs can be measured together. One-byte entries
also share the end of their block head, the earliest entry of their group
that ends with them (below). There is no
partition-parallel interning, or claim that the complete foreign linking
acceptance criteria are met.

## Pool and offset map

Inputs remain immutable. Sections share entries only when their names,
flags and code-unit widths match. An entry with bytes of its own is an
owner. The first equal owner whose destination offset satisfies the later
entry's effective alignment wins. Effective alignment is the original section
alignment reduced by the entry's original offset. A stricter alignment can
require another copy. Padding is zeroed; input order determines the result on
both paths.

### One-byte suffixes

A one-byte entry that equals no suitable owner tries one more place
([#3044](https://github.com/buster14a/buster/issues/3044)), the end of its
block head, where that offset satisfies its effective alignment:
`compiler\0` then `piler\0` is a nine-byte pool, `piler` at offset 3. An
entry's block is the set of one-byte entries of its group, from any
section, whose content ends with its own: equal or longer, owner or not,
the entry included. Its head is the block's smallest input index. When the
head comes first, the entry takes `out[head] + len(head) - len(entry)` if
that offset meets its alignment; otherwise it becomes an owner, at the pool
size rounded up to its alignment. Equal owners come first, and a later,
longer string never moves an earlier one. Two- and four-byte units share
whole entries only; in a group that mixes units only its one-byte entries
form blocks. Only owners are copied, so every pool byte is written once.

Four facts shape the implementation:

- A head is always an owner: nothing earlier equals it or ends with it, so
  neither the equal probe nor a head places it.
- At effective alignment 1 the rule equals the earlier one, which walked
  every earlier, longer owner for the first whose end met the alignment:
  that first owner is then the head.
- The two differ only at alignment 2 or more, when the head's shared offset
  misses.
- Heads depend only on contents and input order: they are minima, so they
  need neither offsets nor a stable sort.

Trying only the head removes the one offset-dependent search the earlier
rule made, and with it the per-content records of earlier owners' offsets.
Where the head's end misses an alignment of 2^t, the entry owns bytes, and
every later entry of the same group and content at that alignment or less
takes them by the equal probe: each (group, content, t >= 1) adds at most
one owner, of at most `len + 2^t - 1` bytes. Over the `SHF_MERGE |
SHF_STRINGS` sections of 14 real inputs, in link order, the pools are the
earlier rule's to the byte:

| Input | Pool bytes | Entries placed differently |
| --- | ---: | ---: |
| libc.a | 50,739 | 227 |
| libcrypto.a | 133,644 | 0 |
| libssl.a | 36,146 | 0 |
| libX11.a | 9,273 | 0 |
| libdb-5.3.a | 107,231 | 4,228 |
| libfreetype.a | 4,037 | 0 |
| `build_regression.o`, clang -O2 | 52,678 | 0 |
| `build_regression.o`, gcc | 55,570 | 0 |
| sqlite's `shell.o`, clang | 61,521 | 0 |
| sqlite's `shell.o`, gcc -Os | 59,377 | 0 |
| sqlite's `shell.o`, gcc | 61,076 | 1,561 |
| `sqlite3.o`, clang | 26,217 | 0 |
| `sqlite3.o`, gcc -O3 | 26,880 | 0 |
| `sqlite3.o`, gcc | 27,024 | 0 |

The test's own random generators, at alignments up to 2^6 and 2^3, change 46
of 192 mixed trials (346,334 pool bytes in all to 346,444) and 66 of 128
suffix-chain trials (25,969 to 27,032). Fixtures 1004 and 1008 change; the
test explains each.

Not every real link keeps its size. gcc -O2 puts the strings it aligns to 8
bytes in `.rodata.str1.8` sections, whose entries start at multiples of 8
and so ask for that alignment, and a head's end there often misses it. Of
41 generated freestanding C programs built by gcc -O2 and linked with
`link-foreign --merge-strings`, 10 have a larger `.rodata` than under the
earlier rule and none a smaller one: 49,507 bytes in all against 48,731,
the largest 2,408 against 2,184 (whole-entry pooling alone gives 54,261,
and no pooling 78,942). The same generator's programs built by clang at
-O0, -O2 and -Os and by gcc at -O0 and -Os, 201 of them, keep the earlier
rule's sizes, and every program prints its expected output. The smallest
such case: `xab` in a section of alignment 1, then `yzab`, an empty string
and `ab` in one of alignment 2. The head of `ab` is `xab`, whose end at 1 is
odd, so `ab` owns three bytes at 10 of a 13-byte pool; the earlier rule took
the end of `yzab`, at 6, for 9 bytes.

### Prefilter, sort and heads

`foreign_strings_block_heads` finds the heads in scratch before interning:

- `foreign_strings_groups` maps the one-byte sections that hold entries to
  dense groups, through an open-addressed map of their group values (any
  `u32`, `UINT32_MAX` included: a slot holds a dense id + 1), and takes each
  group's shortest and longest length with 16-lane `vpminud` and `vpmaxud`
  over its sections' lengths. A group whose one-byte entries all have one
  length is not sorted: an entry's head there is its first equal entry,
  which the equal probe tries. With no sorted group nothing more runs and
  interning is main's; `benchmark-foreign-strings`' `unique` and `duplicate`
  shapes are such inputs.
- `foreign_strings_flag` flags, in sorted groups, every content with a
  proper extension in its group together with its whole block. A key holds
  the last eight content bytes, the one before the terminator most
  significant. The long level marks the (group, last 8 bytes) of each entry
  with at least 8 content bytes as seen and twice seen, in two bitmaps of at least 16
  bits an entry, and flags an entry whose mark was seen twice. The short
  levels mark each 1- to 7-byte receiver's (group, length, bytes), flag
  every longer donor whose last j bytes hit a receiver's mark of length j,
  and then flag the receivers hit. The hash is
  `((t + g * 0xd6e8feb86659fd93 + j * 0xa0761d6478bd642f) * 0x9e3779b97f4a7c15) >> (64 - b)`.
  Flags are a function of (group, content), and a content's block shares
  its last min(k, 8) bytes, so an unflagged entry's head has its own
  content. Empty strings are never flagged; one after its group's first
  one-byte entry takes that entry as head. The flags are a bitset,
  compacted 16 entries a step with `vpcompressd` into an input-ordered list,
  which is allocated before the bitmaps so that the sort reuses their pages.
- `foreign_strings_sort` sorts the flagged entries' 16-byte records (key,
  entry, content) within their groups: by insertion for 32 or fewer, else by
  an MSD radix of 8-bit digits from an explicit work list. A vector pass over
  a range's keys, 8 to a `zmm`, finds the digits in which they vary; a range
  with none is done, and any other is partitioned by its highest varying
  digit between the records and a spare buffer, its buckets of more than 32
  listed and the others sorted by insertion. Each sorted neighbour pair's
  common suffix comes from the keys, 8 pairs to a `zmm` (`vplzcntq`,
  `vpminuq`). Ties past the key's bytes are refined from another work list:
  32 or fewer by insertion with 64-byte masked comparisons from the end.
  A larger one first finds its members' common tail, comparing each with
  its first member 64 bytes a step and at most 256 bytes past the tie's
  depth, each member independently of the others. A tie of one content
  then only sets its common suffixes; any other takes the 8 key bytes past
  that tail and is sorted by them. A member that ends there or differs in
  the next byte splits the tie, so a level splits it unless every member
  shares the 256 bytes, and the next level starts at least 8 bytes deeper.
- `foreign_strings_heads` makes one stack pass over the sorted records,
  whose open classes have increasing contents. A class keeps its block's
  earliest entry, taking its extensions' as they close, and writes it as
  every member's head only where it is not the class's own first entry. The
  interning loop checks a head in O(1).

A model of the prefilter run over the same inputs, which flags the C
prefilter's 1,007 on clang's `build_regression.o`, flags these shares of the
one-byte entries of multi-length groups: `build_regression.o` 1,007 of 1,327
(75.9%) with clang and 852 of 3,835 (22.2%) with gcc; `shell.o` 83.8% with
clang, 84.4% with gcc -Os and 38.6% with gcc; `sqlite3.o` 76.8% with clang,
46.4% with gcc -O3 and 46.6% with gcc; libc 38.4%, libcrypto 64.1%, libssl
39.2%, libX11 47.5%, libdb 33.3% and libfreetype 73.1%; gcc's
`build_regression.o`, `shell.o` and both `sqlite3.o` together 42.4%, and
clang's `build_regression.o` eight times 95.5%.

### What stays scalar

Each part below stays scalar for the reason or count given; none of them has
9700X figures yet, and that measurement is still owed:

- Key loads: one per considered entry and prefilter pass (an 8-byte load
  when eight bytes remain, else a masked one).
- Bitmap marks and tests: at most 2 (long), 7 (donor levels) and 1
  (receiver) an entry. AVX-512 has no bit scatter.
- Radix counts and scatters: one of each for every partition a record
  takes, which is one per key digit that varies in its range, so at most 8
  a sort: a group's slice, or a tie of more than 32 at one level. On Zen 5 a
  `zmm` `vpscatterdd` is 88 uops and `vpgatherdd` 64.
- Insertion sorts by key: every sorted record ends in a slice or radix
  bucket of at most 32, so at most 31 key comparisons and moves a record a
  sort.
- Ties of more than 32: per member and level, one key load and at most four
  64-byte comparison steps with the tie's first member. Every level starts
  at least 8 bytes deeper, so a record takes at most one level per 8 of its
  content bytes past the first key, and a level that does not split its
  tie moves 264 bytes deeper.
- Comparisons in ties of at most 32: bounded by the common tail / 64.
- The stack: one push and one pop per distinct content.
- Head writes: one per member of a class headed by an extension.
- Interning: O(1) more an entry, in the loop that was scalar before.

Benchmark timings come from the 9700X, and these parts have none yet. The
`unique`, `duplicate` and `lengths` shapes link to main's outputs byte for
byte. On `lengths`, which shares nothing, a long bitmap of 32 bits an entry
cut the flagged entries from 588 to 288 at 4,096 literals and from 1,256 to
924 at 131,072. Keys and hashes 8 entries to a `zmm` were also tried. Which
of the two pays is left to a 9700X run; neither is in. Plain 8-byte long
keys, the flag bitset and the reused pages were kept.

The 9700X workflow's foreign-strings step runs every shape.

The vector path scans at most 64 input bytes at a time with masked byte,
word or dword zero comparisons, then hashes eight complete entries at a
time with the existing name hash. The exact path walks code units and hashes
one entry at a time. Both use scalar open-addressed probes with full byte
comparison after a hash match. Entry offsets are ordered per original
section; a binary search translates every interior byte, including the
terminating code unit. One-past-section addresses are not entries.

The producer checks its 32-bit counts and byte offsets, complete final code
units and terminators before publishing a result. A failed build restores
the output arena mark and leaves the caller's result untouched. Borrowed
input bytes and output arena storage must remain live through writing.

## Link consumer

`foreign_strings_merge` runs after COMDAT selection and garbage collection,
with the original per-object section and symbol maps already updated by
those passes. It retains ELF section-symbol identity, which the flattened
Link flags alone cannot express. It applies a pool only if its byte size is
smaller than the original sections' total.

This pass is an explicit consumer choice. The default foreign driver does
not call it. Saving literal bytes does not by itself make the complete link
faster; the measured cost below keeps that choice opt-in.

This is a final-executable pass. The pooled Link must not be sent back to
the object writer as separately linkable ET_REL sections. Generic import
keeps wide string sections as ordinary RODATA because the object writer's
STRINGS representation describes one-byte entries. The final adapter uses
the original ELF metadata to select wide sections, preserving that earlier
import/object-output boundary.

The first selected STRINGS atom owns the pool with unit zero; other selected
atom IDs remain as empty identifiers. The Link checker permits local
references from any input unit to this literal pool. Symbol and fixup IDs,
names, declared sizes and export lists stay stable. Original section maps
continue to identify the input atom; the retained `atom_inputs` column and
offset map translate its original offsets for subsequent consumers.

Named symbols move at their own offset and keep their addends. A declared
object's extent must fit within its original entry. A named absolute or
data-PC64 address must also remain in that entry: crossing a terminator depends on
adjacency that interning can change. Absolute section-symbol expressions
instead translate the original `S+A`, and recompute the addend relative to
the remapped symbol. Expressions at or outside the original section's bounds
retain the unmerged input through unsupported-expression fallback. Size and GOT expressions retain their original
meaning. Changed section addends use the checked 32-bit fixup representation;
pools above `INT32_MAX` are refused by this consumer.

All PC32 and CALL/PLT32 expressions targeting pooled strings are conservatively
unsupported, including named symbols and zero addends. Their addends can
combine a string offset with an instruction's field-end bias, which cannot
be recovered from relocation kind alone. For example, a PC32 addend of zero
can represent an instruction addressing four bytes beyond its named symbol.
No unconditional four-byte adjustment is guessed. Section-symbol PC64 is
also refused in this initial consumer; named PC64 is data-only in Link and
uses the same-entry rule above. Any unsupported expression leaves every Link column and
the output arena unchanged, so the caller can write the unmerged input.

All translated symbol values and changed addends are computed before any
Link mutation. Relocations inside the string storage are unsupported.
The matching Foreign/ForeignLink and producer maps must be valid, and the
pool/map storage must not alias mutable Link columns.

## Validation

`test-foreign` contains an independent interning model, guarded source tails,
distinct output poison patterns, interior-offset checks and rollback cases.
The model finds each one-byte entry's block head by comparing it with the
ends of every earlier entry; it keeps the earlier rule's walk over longer
owners as a second model, which must give the same pools in suffix-chain
trials at alignment 1. Pools and offsets worked out by hand cover a shared
suffix, another group, two-byte units, an alignment the head's end misses,
a later longer string, duplicates of a suffix and of its aligned copy, empty
strings sharing a terminator and beside another group, tails of 7 to 65
shared bytes around the key and comparison widths, a head last in sorted
order, interleaved groups including `UINT32_MAX` and mixed units, ties of
more than 32 re-keyed once, all equal, with a member at the known depth,
with a common tail past two keys or of 300 bytes (past one level's 256 and
bounded only by members that differ there), a staircase headed by its
shortest member that splits a few strings a level, and one key that alone
differs in the upper half of a sort step; strings past 64 bytes, a suffix
chain of twelve, bytes 0x80 and 0xff, the prefilter's level boundaries, an
equal class whose first member's end an aligned member misses, and
one-length groups. Direct calls of the
prefilter and of the build's block-head stage pin how many entries are
flagged and how many heads are written. Suffix-chain trials vary groups and
alignments, and a guard page before the slice checks that every key and
comparison load starts within its entry. A sweep starves the producer's
scratch at every room up to the first that builds each of two fixtures, one
reaching an empty string's head and one the bitmaps, the flagged list, the
records, the work lists and the head pass's heads, each room poisoned first:
every smaller one must fail with a memory error and leave the result, the
output arena's mark and the scratch length unchanged, and the first that
suffices must build the unstarved pool.
Consumer fixtures cover aliases, section and named expressions, unchanged
size/GOT expressions, cross-unit locals and unsupported-expression fallback.
Original assembly fixtures go through foreign resolution, conversion,
pooling and the built-in executable writer at 1, 2, 3 and 8 lanes. Their
executed programs inspect the relocated data and alignment, including the
second object's `x`, which shares the end of the first object's `prefix`;
exact/vector outputs must match. `--cross-check` links and runs the same original inputs
with GNU ld and lld.

`benchmark-foreign-strings` writes deterministic original ET_REL inputs
before timing, then measures resolution, conversion, pooling, writing, cleanup
and their whole interval. The interval includes reservation of the per-link
name arena, release of input mappings and foreign lane storage, release of
the name arena, and reset of the caller's reusable output arena. Each sample starts from fresh input mappings and Link
columns. Every emitted program hashes all referenced literal bytes against
an independent expected checksum; these runs and full-image comparisons
are outside the timed interval, as is removal of the preceding output file.
This is per-link pipeline time, not process startup or frozen-input generation.
Reported retained arena bytes are captured before cleanup, exclude mapped
inputs and transient scratch, and are not peak process memory.

The default cases contain 64, 4,096 and 131,072 named literals in six
shapes. `duplicate` repeats eight values and `unique` has none equal, all of
31 characters, as every campaign below had, so the producer sorts none of
them (#3044). `suffix` and `lengths` have 9 to 25 characters: every second
`suffix` literal is the end of the one before it, and no `lengths` literal
ends another, so that shape pays for the prefilter and saves nothing.
`repeats` is the `suffix` literals with every pair a second time in the
second half (in another unit from 512 literals): duplicates in a group of
several lengths. `tie` is the `unique` literals with the last one `qr`,
which every other ends: one tie of every literal past the sort's first key
bytes. The programs of the four shapes with several lengths hash each
literal through its terminator, since the bytes after a shared end depend on
the pool's layout; `unique` and `duplicate` keep the program that hashes 32
bytes a literal. `--counts`, `--only SHAPE`,
`--runs` and `--order` bound each shared-lock invocation and allow
alternating no-merge/merge and exact/vector order. `--keep` preserves the
frozen objects for hashing and inspection. One lane runs on CPU 2 for these
measurements; CI runs the two smaller sizes of every shape with three samples
as an executable benchmark check.

`--after-scalar MS` accepts 0 through 300 and warms a scalar dependency
chain before each timed pipeline. Its time is excluded. Repeat processes
with zero and 100–300 milliseconds to compare the ordinary pipeline with
the transition after lighter work. The zero setting is unconditioned, not
a pure sustained vector loop: required program execution and byte checks
run between samples. The benchmark reports wall time and
fenced timestamp-counter ticks for the whole interval; TSC ticks are not
core cycles. Inspect the emitted conditioning loop on the target host before
using that control as evidence.

## Measured consumer cost

The [October 23 controlled campaign](https://github.com/buster14a/buster-performance/blob/a00d16e47b6f1090b8a0424ab0dd8cb4e6016713/docs/measurements/foreign-strings-2026-10-23/README.md)
compares frozen baseline `e74b6727` with candidate `aa2e3257`. The candidate
removes redundant entry searches during symbol and named-fixup translation;
the generated named zero-addend ABS64 cases directly exercise that shortcut.
Three process pairs per conditioning state ran under separate 9700X locks,
pinned to CPU 2, with eleven timed links per variant. Times include per-link
reservation and cleanup. Inputs and the page cache were warm.

Every duplicate-and-pool case improved complete link time beyond the observed
three-process ranges. Median reductions were:

| Literals | Exact, 0 ms | Vector, 0 ms | Exact, 100 ms | Vector, 100 ms |
| ---: | ---: | ---: | ---: | ---: |
| 64 | 3.63% | 5.73% | 6.16% | 7.23% |
| 4,096 | 3.95% | 11.97% | 4.85% | 12.10% |
| 131,072 | 2.11% | 4.70% | 3.65% | 4.78% |

No control slowdown had disjoint ranges. Two controls also improved beyond
their ranges, so the evidence does not attribute every timing change solely
to the search removal. Both frozen binaries passed the full foreign
cross-check suite; all twelve processes passed 14,112 validation checks.
External input hashes and complete output comparisons, including unique
no-saving outputs across all four variants, matched. Peak RSS ranges overlap.

Pooling still cost more than omission in all 48 median comparisons. For the
largest duplicate case, candidate vector pooling cost an additional 26.71%
without conditioning and 25.51% after 100 ms; literal storage fell from
4,194,304 bytes to 256. These are gains over the older pooling consumer,
not a reason to enable pooling by default.

The current PR composition retains the measured consumer but includes later
main changes and an untimed no-saving validator improvement. The campaign
predates one-byte suffix sharing, which these literals, all of one length,
never reach. The frozen
measurements do not certify that composition's final-head CI. Nonzero-addend
and section-relative performance, real-corpus gains, parallel interning and
whole compilation remain unmeasured by this campaign. The linked record
preserves exact revisions, ranges, commands, assembly and original logs.

The implementation and fixtures are original project code. LLVM's ELF
symbol-address handling was consulted as a semantic reference; no source or
fixture was copied. Tests and performance measurements must run on the
9700X through its shared lock.
