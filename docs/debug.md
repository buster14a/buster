# DWARF source lines and final addresses

`src/buster/compiler/debug.c` supplies a bounded part of [#88](https://github.com/buster14a/buster/issues/88):
DWARF 5 function names, ranges and source lines. It emits `.debug_info`,
`.debug_abbrev`, `.debug_line`, `.debug_addr` and `.debug_rnglists` in DWARF32
format for Linux x86-64. Types, variables, inline scopes and unwind instructions
are not represented by this interface.

`debug_write` emits the sections before final layout, with zeroes in address
slots. `debug_finalize` later writes the function addresses into `.debug_addr`
and the `DW_LNE_set_address` operand at the start of each line sequence. The
other bytes, including all function ranges and line-program deltas, stay
unchanged. Each range uses an address-table index plus the function size.

## Producer and consumer contract

The producer supplies a `DebugUnit`:

- bounded `String8` names, without embedded NULs, for the compilation directory,
  files and functions;
- a `DebugFunction` per function: name, encoded byte size, file index,
  declaration line, and a contiguous range of source rows;
- two contiguous `uint32_t` row columns: instruction-relative byte offsets and
  source lines. Row ranges partition those columns in function order. Offsets
  are nondecreasing within each function and strictly less than its size.
  Source lines are in `[0, INT32_MAX]`; zero means no source line.

The offsets must be produced **after branch sizing**, from the actual encoded
instruction boundaries. Each function currently has one source file, without
column numbers or row flags. Functions may be in any final address order.
Zero-row functions and an empty unit are valid. Every line sequence explicitly
sets its zero-based file index, including file zero, and ends at the function's
exclusive end.

The consumer supplies a `DebugOutput`: a separate byte slice and capacity for
each of the five sections, plus a `uint32_t` line-address-offset slice with
one entry per function and its explicit capacity. No allocation or scratch
arena is needed by the emitter. Inputs and outputs must not overlap. Each
section is measured before any output bytes are written; a capacity failure
sets the required section sizes and leaves byte buffers and address offsets
untouched. Invalid input and format-limit failures change nothing. No buffer
padding is assumed, including for vector tails.

After a successful write, `debug_finalize` takes that same unit, its unmodified
output, and one `uint64_t` final address per function. It checks all
address-plus-size additions before patching anything. The caller may finalize
again for a different layout. It must keep the unit and output metadata alive
until finalization completes. Different units and their buffers are independent.
Their contributions cannot simply be concatenated: abbreviation offset zero,
statement-list offset zero, address base eight and range-list offset twelve
refer to the start of each unit's own sections. Multiple contributions need
section-offset rebasing in addition to address finalization.

## Native executable attachment

`ExecutableOptions.debug` accepts an `ExecutableDebug` descriptor containing
the five section byte slices, a function `SymbolId` column in their address-table
order, and the line-address-offset column. It currently attaches one contribution.
The producer must keep that function order and its emitted sizes consistent
with the corresponding definitions in `Link`.
The descriptor and bytes stay caller-owned and unchanged. The writer validates
all slices and patch slots and requires ordinary functions defined in TEXT
atoms. It copies the sections into its own arena, then patches addresses from
the actual atom placement and symbol offsets. These are definition addresses,
even where shared-library interposition changes a reference to a function.

The sections join the writer's ordinary piece, hash and output pipeline after
the loaded segments. Their `sh_addr` and allocation flags are zero, and they
do not change loaded-segment sizes, TLS or `_edata`. The writer also emits
`.symtab` and `.strtab` for the descriptor's functions, with locals before
globals, their original binding/visibility and actual sizes. This lets perf
resolve function names as well as DWARF source lines. Dynamic exports are
unchanged. An explicit `ExecutableOptions.symbols` selection can supply the
regular table independently of DWARF, or override its function-only selection;
the debug address slots still follow the debug descriptor. With neither
attachment, none of these seven sections is emitted. See
[the regular-symbol contract](executable-symbols.md).

The consumer fixture exposed an existing loaded-segment layout issue in perf
7.2.9 with libdw: compact file offsets made its mapping-base calculation differ
between segments, and every native sample failed with overlapping modules.
The writer now keeps each loaded segment's file offset equal to its relative
virtual address. This preserves virtual addresses and page congruence, while
adding page-multiple file gaps, at most 12 KiB across the three transitions.
The existing piece writer zeroes and hashes those gaps. This correction applies
with and without a debug descriptor. Linux perf's
[libdw module reporting](https://github.com/torvalds/linux/blob/master/tools/perf/util/unwind-libdw.c)
uses the executable mapping's start minus its file offset as the module base.

The format constants follow the [DWARF 5 specification](https://dwarfstd.org/doc/DWARF5.pdf),
sections 3.1, 3.3, 6.2, 7.5, 7.27 and 7.28. This is original implementation and
fixture code; no external implementation or generated external bytes are
incorporated.

## Row emission and measurement

The vector path forms four address and line deltas together. Leading-zero
counts determine their LEB128 byte counts. `vpmultishiftqb` spreads the
seven-bit groups, a byte permutation inserts the three standard line opcodes,
and `vpcompressb` packs the valid bytes directly into the final line program.
The store is masked to exactly the byte count. The scalar twin writes each
opcode and LEB128 value independently and handles partial groups.

`benchmark-debug [--functions N] [--rows N] [--only scalar|vector]
[--after-scalar MS]` measures validation, size calculation, all five section
writes, and final address patching together. It compares the complete outputs
before timing. Its default is 4,096 functions with 64 rows each, 21 alternating
passes, and reused caller-owned output buffers. It reports elapsed nanoseconds
per row, the range across passes, milliseconds per unit, output bytes, byte
throughput and hardware user cycles per row. Denied or multiplexed cycle counts
are reported as unavailable. The emitter needs no scratch buffer; caller-owned row columns cost
eight bytes per row and late-address offsets cost four bytes per function.

This input is synthetic. Its construction is outside the timer; source-to-row
mapping, code generation and file I/O do not exist in this measured path. These
numbers cannot establish whole `-g` compilation performance or a benefit from
streaming during compilation. Run each build, test and measurement under the
9700X's shared lock, with measurements pinned to CPU 2.

On October 23, 2026, three separate processes on the approved 9700X, each
locked and pinned to CPU 2, measured frozen head
`7966b9f3fa87b4d3a71e0cc96262ef009c1a23ee`. The ranges below are the three
process medians, each based on 21 passes:

| Functions × rows | Output bytes | Scalar ns/row | Vector ns/row |
| --- | ---: | ---: | ---: |
| 16 × 4 | 1,232 | 6.753–6.806 | 5.689–5.714 |
| 4,096 × 64 | 1,523,425 | 4.070–4.095 | 2.143–2.148 |
| 65,536 × 64 | 24,583,906 | 4.174–4.192 | 2.207–2.226 |

The default vector runs used 11.879–11.907 user cycles per row, versus
22.551–22.671 for scalar. Separate scalar-only processes measured
4.072–4.124 ns/row; vector-only processes measured 2.141–2.152 ns/row.
Vector measurements after 100 ms of scalar emission were 2.146–2.157 ns/row.
The larger case uses 32 MiB of row input plus roughly 23.4 MiB of output,
exceeding the processor's 32 MiB L3. These are complete emitter measurements
with reused buffers, not cold-cache or whole-compilation claims. The built
compiler's SHA-256 was
`2277f30a1d90d71f6a53782dcc57179e9566f81d01d7765e75754d9108798966`.

## Checks

`test-debug` compares the scalar and vector bytes, independently decodes every
line row, tests unsigned and signed seven-bit boundaries and integer extrema,
checks every row count through 129, and puts both input columns and the output
at protected page boundaries for counts through 65. It checks all section
capacity failures, invalid row/file/string inputs, empty units and sequences,
address overflow before any patch, and repeated finalization.

`test-debug --cross-check [--keep]` also produces an original three-function
ELF fixture. GNU as supplies its machine code and CFI; both GNU ld and Buster's
`executable_write` attach the debug sections, the latter with its own final
layout and address patches. The native fixture also checks scalar/vector and
lane-count identity, poisoned prior output, malformed descriptors and unchanged
caller bytes. `readelf` reads the sections,
GDB sets a breakpoint by source file and line and checks all three stack
frames, and `perf record --call-graph dwarf` plus `perf script` must report a
single sampled leaf-to-outer-to-start call chain with its source line. External
command failure, missing samples or missing consumer tools fail the check.
This validates the new debug bytes and native writer attachment with real
consumers; GNU as remains the fixture's unwind-information producer.

The [required 9700X run for PR #329](https://github.com/buster14a/buster/actions/runs/39937746848)
passed on October 23, 2026. It tested merge result
`b053b9a7a429a5f26f8b9f3c940b491e942f161e`, combining submitted head
`7966b9f3fa87b4d3a71e0cc96262ef009c1a23ee` with main
`6d79b50760b1e93f74d0abd8f2f20b4d6f2b081a`. All 47 test commands passed,
including 103 native debug checks, GDB and perf for both GNU ld and native
output, and 48 loaded-segment mapping checks. The executable, dynamic, static,
foreign and no-recursion checks also passed. This is merge-result correctness
evidence; the workflow's single emitter timing is not repeated CPU-2 performance
acceptance or a whole-compilation measurement.

A separate build of the exact submitted head, from a 149-file source manifest,
also passed `test-debug --cross-check --keep` under the shared lock on CPU 2.
It passed the emitter and protected-tail checks, all 103 native attachment
checks, and the GDB/perf checks for both GNU ld and Buster output. This is
standalone-head consumer evidence, separate from the hosted merge result.

## Remaining integration for #88

`Link` has no debug-section atom classes and the importer still drops
nonallocated sections: the new native attachment takes explicit caller bytes.
Preserving arbitrary foreign DWARF and rebasing multiple independent units
remain follow-ups. This module does not modify the assembler-owned CFI/section
adapter in #233.

The merged [Tier 0 PR #303](https://github.com/buster14a/buster/pull/303),
rechecked on main at `9857e71781447f697da87809f3262b15a82145f6`, contains real
prologue and epilogue production. `Tier0Frame` records the frame size and
callee-save mask; `tier0_emit_prologue` pushes those registers and reserves
the frame, and `tier0_emit_epilogue` reverses both. However, `Tier0Code`
exports only instruction records, branch columns, symbolic references and
status. It does not export source-entry-to-record locations, the frame
description or the record ends of the saves, stack adjustments and restores.

Source-row integration still needs the Tier 0 producer to retain source-entry
to instruction-record locations, then convert those positions to byte offsets
after branch sizing. The separate [native unwind adapter](unwind.md) derives
bounded fixed-frame events from valid, finalized Tier0 instruction records.
It is opt-in and does not decode emitted bytes or repeat the producer's generic
validation. Its additional effects/control-flow scan is charged in the
backend-chain benchmark. The producer itself is unchanged.

Main already merges `.eh_frame` and indexes it with `.eh_frame_hdr`, including
scalar/vector validation and runtime unwind tests. The native unwind module
supplies per-function CIE/FDE contributions for that existing path; its guide
records validation and measurement status. Data-first segment
ordering, writing while compilation is in progress, its gdb/perf checks and
the comparison against writing at the end remain separate work. Issue #88
remains open for these integration and measurement requirements.
