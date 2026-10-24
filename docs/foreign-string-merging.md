# Foreign string merging

`foreign_strings.c` interns complete zero-terminated entries from read-only
ELF sections marked `SHF_MERGE | SHF_STRINGS`. One-, two- and four-byte code
units are supported. A zero byte inside a nonzero wide code unit does not
terminate an entry. Other entry widths retain their unmerged storage.

This is a bounded part of [#86](https://github.com/buster14a/buster/issues/86).
The offset-map producer and Link consumer are separate so their correctness,
storage and whole-pipeline costs can be measured together. There is no suffix
merging, partition-parallel interning, or claim that the complete foreign
linking acceptance criteria are met.

## Pool and offset map

Inputs remain immutable. Sections share entries only when their names,
flags and code-unit widths match. The first equal entry whose destination
offset satisfies the later entry's effective alignment wins. Effective
alignment is the original section alignment reduced by the entry's original
offset. A stricter alignment can require another copy. Padding is zeroed;
input order determines the result on both paths.

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
Consumer fixtures cover aliases, section and named expressions, unchanged
size/GOT expressions, cross-unit locals and unsupported-expression fallback.
Original assembly fixtures go through foreign resolution, conversion,
pooling and the built-in executable writer at 1, 2, 3 and 8 lanes. Their
executed programs inspect the relocated data and alignment; exact/vector
outputs must match. `--cross-check` links and runs the same original inputs
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

The default cases contain 64, 4,096 and 131,072 named literals, with either
eight repeated values or all unique values. `--counts`, `--only duplicate`
or `--only unique`, `--runs` and `--order` bound each shared-lock invocation
and allow alternating no-merge/merge and exact/vector order. `--keep`
preserves the frozen objects for hashing and inspection. One lane runs on
CPU 2 for these measurements; CI runs the two smaller sizes with three
samples as an executable benchmark check.

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
main changes and an untimed no-saving validator improvement. The frozen
measurements do not certify that composition's final-head CI. Nonzero-addend
and section-relative performance, real-corpus gains, parallel interning and
whole compilation remain unmeasured by this campaign. The linked record
preserves exact revisions, ranges, commands, assembly and original logs.

The implementation and fixtures are original project code. LLVM's ELF
symbol-address handling was consulted as a semantic reference; no source or
fixture was copied. Tests and performance measurements must run on the
9700X through its shared lock.
