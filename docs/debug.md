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
column numbers. A function may mark the row where its body starts
(`DebugFunction.prologue_end`, one more than the row's index, 0 for none):
`DW_LNS_set_prologue_end` goes before that row, where gdb (13 and later) and
lldb put a breakpoint on the function. Functions may be in any final address
order.
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
A unit alone points its header and DIE at the starts of its own sections
(abbreviation offset zero, statement-list offset zero, address base eight and
range-list offset twelve). A unit written after others gives
`DebugOutput.bases`, where its contribution starts in each section of the file,
and those four offsets point there instead, so that units concatenate; every
other byte is the unit's alone, and a base that would carry a contribution past
DWARF32 is refused before anything is written. `test-debug` compares a unit
written at bases with the same unit alone, byte for byte but the four offsets.

## Native executable attachment

`ExecutableOptions.debug` accepts an `ExecutableDebug` descriptor containing
the five section byte slices, a function `SymbolId` column in their address-table
order, and the line-address-offset column. It attaches one unit or several
(#395): `unit_functions` and `unit_count` give each unit's function count, each
section's slice holds the units' contributions one after another (each unit
written at its `DebugOutput.bases`), the line addresses are offsets in the whole
`.debug_line` slice, and the writer finalizes each unit at its functions'
addresses, unit k's `.debug_addr` contribution after the earlier units'
8 + 8n bytes. A descriptor whose counts don't add up to the functions, or don't
match the units' `.debug_addr` headers, is refused before anything is written.
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

A producer that builds whole debug sections, as `link-foreign` does from other
compilers' objects (`foreign_debug.c`), passes `ExecutableOptions.debug_sections`
instead: each DWARF section's bytes by kind, every reference between debug
sections resolved, and the references whose values the layout decides, as
columns (each field's offset, addend, target symbol, section kind and kind).
The writer patches those on its lanes after the layout: a definition's address
(not a stub's), a thread-local's offset in the TLS segment, or lld's tombstone
for a reference to discarded code (all ones in `.debug_names`, 1 in
`.debug_ranges` and `.debug_loc`, 0 elsewhere).
The driver's contract ("Debug information") has the rest.

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

## The C driver's `-g` (#395)

`./build/compiler -g -o program file.c` links an executable with a unit of
the program's source functions (`c_check_debug.c`), which
`ExecutableOptions.debug` attaches. Its rows come from three passes, each a
vector path with an exact twin that `--scalar` takes, into the same bytes:

- **Raw rows**, in `check_program_emit` after branch sizing: each typed
  entry's first record (`Tier0Code.entry_records`) at its byte offset, with
  the token whose lowering wrote it (`CheckProgramLowered.entry_tokens`;
  saved with each lowering task so that an operation's entry takes its own
  node's token, not its last operand's, and a call's its callee's, as GCC and
  clang place a call across lines), kept where the next entry's code
  starts elsewhere: an entry fused into its consumer starts none of its own.
  The prologue's row, at offset 0, takes the declaration's token, and the
  epilogue's the closing brace's.
- **Lines**: a row's token's presumed line, as the diagnostics place it:
  where its outermost macro expansion was invoked, or where it was written,
  after `#line`. The places of a function's declaration and closing brace
  bound every row's searches: a row between them in one file's range, with no
  `#line` between them, finds its history run, its macro chain's place and
  its line 16 rows at a time over just the function's runs and lines. A row
  from a file the body includes, or past a `#line` inside it, or with a chain
  of more than 8 links, takes the exact path.
- **Kept rows**: those in the function's file (a line sequence has one; a
  row of an included file's code is left out) whose line differs from the
  kept row before's, and the row where the body starts, whatever its line,
  which the function marks `prologue_end`. A file is one key per name (a
  hash table of the files' paths and the `#line`s' names), so that two
  `#line`s naming one file keep their rows together, and the unit's file
  table is built by key without comparing names.

The lowering and its loop over tasks are each compiled twice from one
always-inlined body (`check_program_lower_body`, `check_lower_drain_body`):
`check_program_lower` and `check_lower_drain`, which every compile without
`-g` runs, keep no tokens, and `check_program_lower_sources` and
`check_lower_drain_sources`, `-g`'s, keep them. The flag is a constant in
each, so that the plain copies are the code a compile ran before this
feature. It cost every compile while it was a value: clang kept the flag and
the tokens' column live across the lowering's loops and spilled more, and a
branch on it at every entry cost about as much as the store it skipped, as
did saving the token with every push and restoring it at every pop. Only
`-g`'s drain sets each task's token as it pops it: an expression's node's, a
statement's own, or for any other task the one it was pushed under. The
tokens settle lazily (`check_lower_settle`): when the token changes, the
entries written and the tasks pushed since it last changed take it, so that
no entry or push spends anything on them. The helpers both copies share only
test the flag where a call or an initializer part changes the token in the
middle of a task, and at a function's start and end. Each copy inlines the
tasks' work and `tier0_validate` as clang inlines them into code it compiles
once (six task functions are `always_inline`, and the lowering calls
`tier0_validate_inline`): with a call in each copy, it left them out of line,
and a compile without `-g` took 2% more cycles. The
emission reserves the raw rows and writes each function's out of line
(`check_program_emit_reserve_rows`, `check_program_emit_body_rows`). On the
9700X, a compile without `-g` of a generated program of 31,269 lines retired
0.004% more instructions than main, took 0.42% more wall time and wrote
main's executable; with `-g` it took 7.2% more
([the record](https://github.com/buster14a/buster-performance/tree/fec2430fdab625676f82a1226607b3ca885b1288/docs/measurements/c-driver-dwarf-395-2026-10-25)).

What `-g` adds to the emission is mostly its copy of the lowering (#456). On
`benchmark-check-debug --only emit`'s program (2,000 functions, 101,493 typed
entries, 60,031 raw rows), the emission with the raw rows took 4.4 to 4.8%
longer than without on the 9700X (6.32 against 6.06 ms) and retired 6.24
million more of its 132.2 million user instructions. The copy that keeps the
tokens is 5.61 million of them, about 28 for each of the 197,000 tasks the
lowering pops, which settles the entries and tasks since the pop before and
picks the next token; the raw rows take 0.54 million, Tier 0's entries' first
records 0.10 million, and their room nothing measurable. The 1.1 to 1.2% that
#426 measured with `-g` in a unit of its own compared two lowerings that both
kept tokens: there the benchmark ran in that unit, whose lowering saved the
token with every push and set it at every pop, rows or none. Of the cheaper forms tried, reading the settle's
columns and bounds once (clang read them again after each store through the
writer) saved 0.33 million instructions, but on the 9700X it left the emission
with the rows as long and moved the plain loop's code, which then took 0.8%
longer; doing the work before the pop, or out of line, retired more. The cost
stays `-g`'s
([the record](https://github.com/buster14a/buster-performance/tree/6e5eec7b1354d03073684fe52a82ebc1dcd10f2d/docs/measurements/debug-row-emission-456-2026-10-26)).

A function is named by its identifier, at its declaration's line, in the
file of its declaration (a header's for a function a header defines, a
`#line`'s name after one); the unit's first file is the translation unit's.
`-c -g` writes an object without DWARF: the object writer has no debug
sections or relocations for them yet. `test-check-debug [--cross-check]`
checks each pass's twins (at `PROT_NONE` pages), the lines against
`pp_presumed` on fixtures and generated programs, the fixtures' functions'
rows, and with `--cross-check` the executable through `llvm-dwarfdump
--verify` and gdb: a line's breakpoint, a function's on its first
statement, a step over an untaken branch and a backtrace's lines, the
header's function in the header.

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
ELF fixture, as one unit and as two (its first function's, and the other two's
written at the first's sizes). GNU as supplies its machine code and CFI; both GNU
ld and Buster's `executable_write` attach the debug sections, the latter with
its own final layout and address patches, the two units in one descriptor.
Descriptors whose unit counts don't add up, or move or don't match a unit's
`.debug_addr` header, are refused. The native fixture also checks scalar/vector and
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

## Remaining integration

[#395](https://github.com/buster14a/buster/issues/395) attaches several units
through `executable_write` (above) and keeps other compilers' DWARF through
`link-foreign` (the driver's contract, "Debug information"); `Link` still has no
debug-section atom classes, since those sections are built whole beside it.
This module does not modify the assembler-owned CFI/section adapter in #233.

The merged [Tier 0 PR #303](https://github.com/buster14a/buster/pull/303),
rechecked on main at `9857e71781447f697da87809f3262b15a82145f6`, contains real
prologue and epilogue production. `Tier0Frame` records the frame size and
callee-save mask; `tier0_emit_prologue` pushes those registers and reserves
the frame, and `tier0_emit_epilogue` reverses both. `Tier0Code` exports
instruction records, branch columns, symbolic references, status and each
typed entry's first record (`entry_records`), which the C driver's `-g` turns
into byte offsets after branch sizing and source rows ("The C driver's `-g`"
above). It does not export the frame description or the record ends of the
saves, stack adjustments and restores. The separate [native unwind adapter](unwind.md) derives
bounded fixed-frame events from valid, finalized Tier0 instruction records.
It is opt-in and does not decode emitted bytes or repeat the producer's generic
validation. Its additional effects/control-flow scan is charged in the
backend-chain benchmark. The producer itself is unchanged.

Main already merges `.eh_frame` and indexes it with `.eh_frame_hdr`, including
scalar/vector validation and runtime unwind tests. The native unwind module
supplies per-function CIE/FDE contributions for that existing path; its guide
records validation and measurement status. `streaming.c` (#88) writes a Tier 0
program while it compiles, in the data-first segment order, with these
sections: a unit per 256 functions, made as they compile and finalized at
their addresses, written after the units before it. `test-streaming
--cross-check` has `llvm-dwarfdump --verify` check the units together, gdb
give every frame its function's line and perf's samples lines; the linker
design's "Streaming the output" has the comparison against writing at the
end. Its programs are generated typed streams, without source tokens, so its
rows stay a line per function. The direct path's executable of the same code
takes the same units through `executable_write`, which finalizes each at its
functions' places.
