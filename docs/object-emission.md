# Object emission: planned writing and its work ledger

`object_write` (`src/buster/lib/compiler/object/object.c`) serializes the
format-neutral `ObjectFile` into ELF32, ELF64, COFF or Mach-O bytes. This guide states
the contract of the planned ELF64 writer, how every writer's work is counted,
and what remains to be moved onto the same plan. The measured history and the
staged redesign of the whole path, from machine code to the published file,
are in the object-emission design issue and the audit that introduced this
writer.

## ELF32 i386 relocatable objects

`object_format_for_target` selects `OBJECT_FORMAT_ELF32` for i386 Linux and
freestanding targets. The shared planned ELF writer emits little-endian
`ELFCLASS32`, `ET_REL`, `EM_386`: a 52-byte header, 40-byte section headers,
16-byte symbols and 8-byte `SHT_REL` rows aligned to four bytes. The reader
checks the class and machine together.

| ELF type | Neutral kind | Field width |
|---|---|---|
| `R_386_32` (1) | `ABSOLUTE32` | 4 |
| `R_386_PC32` (2) | `I386_PC32` | 4 |
| `R_386_PLT32` (4) | `I386_PLT32` | 4 |
| `R_386_GOTOFF` (9) | `I386_GOTOFF` | 4 |
| `R_386_GOTPC` (10) | `I386_GOTPC` | 4 |
| `R_386_16` (20) | `I386_16` | 2 |
| `R_386_PC16` (21) | `I386_PC16` | 2 |
| `R_386_GOT32X` (43) | `I386_GOT32X` | 4 |

PC, GOT and PLT formulas remain distinct from the x86-64 kinds. The writer
stores `ObjectRelocation.addend` in the relocated field, replacing its input
bytes without modifying the source payload. REL import normalizes absolute
addends as unsigned field values and other addends as signed values; a foreign
`SHT_RELA` input retains its signed explicit addend and rewrites as REL.
Addends that cannot fit their field, offsets/sizes outside the 32-bit file
domain, symbols beyond the 24-bit relocation index and extended section
numbering are refused before image allocation. Relocation bounds use the
original input contribution, including when several input sections merge.

Four-byte initializer entries preserve preinit/constructor priorities across
import and emission. Ordinary sections, zero-fill, symbol binding/visibility,
the GNU stack note and eager/lazy archives use the shared ELF policies.
Informational x86 GNU properties use four-byte ELF32 padding; mandatory or
unknown properties remain refused. A
borrowing write copies each relocated ELF32 payload so its inline addends can
be filled; unrelocated large payloads may still be borrowed. Its statistics
count each inline store as a patch. ELF64 keeps its separate RELA addends and
single-store payload contract.

`object_test_elf32`, `object_test_elf32_payloads` and
`object_test_elf32_archives` cover original raw records, exact wire fields,
all eight kinds, round trips, hostile inputs, field limits, borrowed/contiguous
equality and initializer ordering. On Linux x86-64 with GNU binutils installed
under `/usr/bin`, `object_test_elf32_gnu` imports an independently assembled
object, checks the rewrite with `readelf -h -S -r`, links both objects with
`ld -m elf_i386`, at fixed text/data/GOT addresses, and compares their relocated text/data bytes. A missing
binutils installation is reported as not run.

This object API does not enable i386 driver code generation, Buster image
linking, in-process i386 execution, code16 instruction encoding or DWARF
generation. Those remain separate backend capabilities. The wire layout
follows the [generic ELF specification](https://gabi.xinuos.com/elf/02-eheader.html)
and [Intel386 psABI](https://github.com/hjl-tools/x86-psabi/wiki/intel386-psABI-1.1.pdf).

## ELF64: plan every range, then store each byte once

Every ELF64 object carries one empty, nonallocated `SHT_PROGBITS`
`.note.GNU-stack`. Its `SHF_EXECINSTR` bit is clear for C code and ordinary
assembly. An explicit assembly `.section .note.GNU-stack,"x",@progbits`
sets that bit; `""` leaves all flags clear. The assembler treats this as
metadata, accepts no payload in it, and repeated declarations retain any
executable request. `-S` prints the same declaration. The generated note is
an extra fixed header and a 16-byte name, without a payload or allocated
section in `ObjectFile`.

The ELF reader records an input note's executable bit before skipping
nonallocated sections. `ObjectFile.requires_executable_stack` survives
merges and selected archive members. Native image linking and the JIT
(`JIT_ERROR_EXECUTABLE_STACK`) refuse an explicit request: executable stacks
are unsupported, and the driver diagnostic names the requesting object or
archive member. No image is published for that input. A missing note is accepted as nonexecuting,
matching LLD's policy; Buster does not infer GNU ld's target-dependent
executable default. Existing Buster ELF images retain their stack policy
(RW on dynamic images; no stack header on the static writers).

The convention and ELF section flags follow the primary
[GNU ld options contract](https://sourceware.org/binutils/docs/ld/Options.html)
and [GNU as section contract](https://sourceware.org/binutils/docs/as/Section.html).
No external implementation code is reused.

`object_write_elf` runs in two phases over the object after the priority
split (`object_split_initializer_priorities`). The split appends one section
per constructor/destructor priority group and records where each grouped
initializer entry went; it does not copy the relocations. When the split
grouped an entry (`object_initializer_split_grouped`), both phases place each
relocation through `object_initializer_relocation_place` as they reach it.
Without a group they read each relocation as it is and place nothing (#990).
The split's tables are sized by sections and entries, and a split past the
section limit or the arena is refused like the plan's own limits.

1. **Plan** (`object_elf_plan`). One pass over the relocations counts each
   section's and the number of RELA tables; one pass over the symbols counts
   the locals, sizes `.strtab` and marks sections named by definitions; one pass
   over the sections maps retained inputs to emitted ELF section numbers and
   assigns their name/payload offsets. Every offset and size is a checked `u64`
   sum. Empty, unreferenced canonical model slots are omitted; nonempty storage,
   zero-size definitions, custom names and additional sections are retained.
   The plan refuses, before any image exists:
   - a relocation kind with no ELF type (`OBJECT_ERROR_UNSUPPORTED_TARGET`);
   - a section count that reaches `SHN_LORESERVE` (`0xff00`), which `e_shnum`
     and `st_shndx` cannot state without extended numbering;
   - a `.strtab` or `.shstrtab` with a byte past a 32-bit offset;
   - an offset or size sum that overflows, or a file the arena cannot hold
     (all `OBJECT_ERROR_CAPACITY`).
2. **Emit** (`object_elf_emit`). The image is exactly the planned size. Each
   range -- header, payloads with their padding, each RELA table, the null
   symbol, the locals, the globals, `.strtab`, and `.shstrtab` through the
   section headers -- is filled front to back by one `ObjectImageRange` cursor
   bounded by the plan. A relocation is written at its section's running
   cursor, so each table is grouped by section in input order without a sort
   or a per-section rescan. A symbol is written at the next slot of its binding
   class, which is ELF's locals-then-globals order, in the same pass that
   writes its name. Nothing is zero-filled and patched later.

The ranges tile the file, so a result whose every cursor stopped exactly at its
range's end had every byte written exactly once. Only such an image is
returned; any error returns no bytes. The plan's tables and the priority split
live in a scratch arena, so the caller's arena keeps only the image.

**Borrowed payloads (`-c`).** `object_write_borrowing`, which the driver's
`-c` uses, is the same writer with one change in the emit phase. A payload of
at least `OBJECT_BORROWED_PAYLOAD_MINIMUM` (4 KiB) is named in place
(`object_image_borrow`) instead of stored. Its range of the image is reserved
but never written, so its pages are never touched, and the artifact records
the payload's `(offset, bytes)` in file order. `object_artifact_slices` yields
the file as the image's own ranges interleaved with those payloads, and
`file_publish_slices` writes them with the same staging, flush and atomic
replace as `file_publish`. The file is byte-identical to `object_write`'s
image. The payloads are ranges of the caller's `ObjectFile` (the priority
split only narrows them), so the `ObjectFile` must outlive the publish. The
borrowed table is allocated in the caller's arena, and only when some payload
qualifies. A borrowing result is complete in the same sense as above: every
cursor reached its range's end, by stores and borrows together. COFF and
Mach-O never borrow. `compiler_driver_test_object_borrowed_payloads` compares
the published file, the slices and `object_write`'s image, and holds the
ledger identities below.

Every ELF writer emits one empty, nonallocated `.note.GNU-stack` section last.
The registered `object_test_elf_stack_contract` checks the raw note header and
name on both native architectures, reader/writer round trips, malformed-note
refusals and the missing-note case, and the configured host-linker boundary
fixture validates the declaration independently.

[#1288](https://github.com/buster14a/buster/issues/1288) intentionally changes
ELF output by removing unused canonical empty sections. The plan's input-to-ELF
map is applied to payload/name offsets, symbol `st_shndx` and RELA `sh_info`
after initializer-priority splitting. Generated format tables remain present.
The section-count guard remains conservative over the input count; compaction
does not extend the supported index range. Borrowing and the work ledger retain
their existing contracts.

The obsolete pre-plan writer and its private test API are retired. The
registered `object_test_elf_planned_writer` now independently decodes section,
symbol and RELA fields for the same seeded/adversarial shapes, including split
priority groups, duplicate names and arbitrary 64-bit symbol values. Those
values need not fit their sections, so this is a raw serialization comparison;
valid public-reader and native-link/runtime controls are separate. Capacity,
error, exact-store and linear-visit assertions remain. The historical
[reference probe](https://github.com/buster14a/buster/blob/353c338398173120a36bbe18221d6ce032ec4cd3/docs/performance-audits/evidence/2026-09-27T021030Z/reference_probe.py)
is unchanged and belongs to that pinned pre-compaction source.

On import, bounded symbol-table discovery marks sections named by definitions
before payload alignment is merged. An unreferenced, zero-size canonical
contribution or legacy empty readonly `.pdata`/`.xdata` contributes no padding
or alignment. Its original bounds/alignment are still validated. Named/custom
contributions and referenced empty definitions retain their alignment and
identity. `object_test_elf_empty_sections` and `object_test_elf_empty_reader`
check raw indexes, zero-size/TLS definitions, RELA addends, borrowed/contiguous
file equality, identity and malformed-input refusals on both architectures.
`compiler_driver_elf_empty_tests` checks source/direct-object path parity and
uses configured native LLD to require no `PT_TLS` for a no-TLS object, with an
initialized/zero-fill TLS runtime positive control.

## ELF64 reader refusal diagnostics

`object_read_elf` reports an unsupported relocation through
`ObjectFile.diagnostic` together with `OBJECT_ERROR_UNSUPPORTED_TARGET`.
The message includes the architecture and unsigned ABI type number.
AArch64 short test/conditional branches (TSTBR14 and CONDBR19) and the
TLSDESC families also include their ABI names. Unknown AArch64 types keep
the architecture and numeric type; existing x86-64 messages retain their
wording. The relocation-kind mapping remains the support authority, including
the supported AArch64 GOT page and low-offset forms. Offset and
bounds validation retain their existing refusal behavior.

`compiler_driver_archive_test_aarch64_refusal_diagnostics` specifies raw
ELF bytes independently of the writer, exercises selected and unused members
through both archive extraction paths, and checks direct/lazy driver records
and output preservation. On native Linux AArch64, a configured host assembler
independently produces the conditional-branch refusal input.

## ELF64 import semantics

`object_read_elf` accepts allocated PROGBITS/NOBITS payloads, supported
unwind records and init/fini arrays. Exactly the section named `.preinit_array`
(of type `SHT_PREINIT_ARRAY` or `SHT_PROGBITS`) is a preinit array, as for `ld`
and `lld`; it takes `IR_INITIALIZER_PRIORITY_PREINIT`, which
`IR_INITIALIZER_PRIORITY_ORDER_KEY` sorts ahead of every constructor priority,
a dependency's `constructor(0)` included, whatever the input order. The sentinel
exists only in `ObjectFile.initializer_priorities` of ELF objects; the COFF
writer spells it as priority zero and Mach-O states no priorities, so it never
reaches either as a name or a number. A `.preinit_array.5` or a type-16 section
of another name is ordinary data, as the host linkers leave it, and the
assembler refuses a `@preinit_array` section of another name. A preinit entry
in a `-shared` output is refused with a named diagnostic, as `ld` does.

A dynamic executable (x86-64 fixed-address or `-pie`, AArch64, or either
Android architecture's PIE) publishes its preinit entries through
`DT_PREINIT_ARRAY`/`DT_PREINIT_ARRAYSZ`, so the loader runs them with argc,
argv and envp before the constructors of the shared objects it loads, as for
`ld` and `lld` (#1243). `link_elf_preinit_array_split` moves the leading
preinit run of the merged constructor array onto the end of the writable data
with its relocations, so the entry stub calls only the constructors. A static
executable has no loader and no dependency constructors; its entry stub still
calls the preinit entries first. Android executables are always PIEs, so each
array entry carries `R_X86_64_RELATIVE` or `R_AARCH64_RELATIVE`, as appropriate.
The Android CI job executes an x86-64 image under bionic and checks the order
(`android/runtime_order.sh`).
See the
[ELF initialization contract](https://gabi.xinuos.com/v42/elf/08-dynamic.html#initialization-and-termination-functions).

The bounded refusal repair for #1243 rejects unsupported allocated section
types, legacy `.ctors`/`.dtors` and their priority families, `.init`/`.fini`
fragments, and exception tables the reader previously discarded. An unsupported
allocated note is refused too; its contract must be understood before it can be dropped.
The GNU property section is understood as a bounded walk. It is a sequence of
`NT_GNU_PROPERTY_TYPE_0` notes (owner `GNU`, eight-byte padding), and each
descriptor is a sequence of properties whose headers and padded data must lie
inside their descriptor and section. A section may hold several notes; GCC
writes a `FEATURE_1_AND` note beside the `*_USED` note under
`-fcf-protection=full`. Two kinds of record are dropped: the optional x86 IBT/SHSTK or
AArch64 BTI/PAC/GCS `FEATURE_1_AND` record, whose output feature intersection is
zero because Buster's generated code does not assert those features, and on
x86-64 the informational `GNU_PROPERTY_X86_ISA_1_USED` (`0xc0010002`) and
`GNU_PROPERTY_X86_FEATURE_2_USED` (`0xc0010001`) records, which a linker drops
whenever any input lacks them. The binutils 2.47 assembler writes the `*_USED` records by default
([#3175](https://github.com/buster14a/buster/issues/3175)). Unknown bits in `FEATURE_1_AND`, any other
property type (including `GNU_PROPERTY_X86_ISA_1_NEEDED` and other required
records), a property size other than four, truncated or overlong note and
property sizes, trailing bytes, other note formats, and unsupported
flags/alignment are refused.
Unallocated unknown metadata and unsupported debug section types retain their
skip policy. Supported DWARF payloads still pass through without DIE decoding.

Ordinary NOTYPE/OBJECT/FUNC/SECTION/TLS symbols keep their existing mapping.
STT_FILE records are metadata and may use SHN_ABS. Other reserved section
definitions (including absolute/common values and extended indexes) and
unsupported runtime symbol types are refused. Dropping a weak absolute
definition would produce a successful link with different behavior.

A GNU IFUNC (`STT_GNU_IFUNC`) defined in code is represented: the reader marks
the symbol `indirect`, and `link_elf_indirect_functions_rebind` rewrites it
before any writer runs, the way `ld` and `lld` give it a PLT entry and an
`IRELATIVE` slot. The name defines a thunk that jumps through a writable slot,
so calls, address-taken references and GOT loads all reach one canonical
address and never the resolver. A runner calls each resolver once and fills the
slots. In an executable it is the first `.preinit_array` entry, which a dynamic
image publishes as `DT_PREINIT_ARRAY`, so the loader runs it after relocation
and before every constructor; a static image's stub calls it first. A
`-shared` object has no runner: each slot carries an `R_X86_64_IRELATIVE`, the
last of its dynamic relocations, so ld.so fills it while relocating the library,
before any initializer runs, including an executable's `DT_PREINIT_ARRAY`
callbacks that call into the library. On AArch64 the runner passes glibc's
resolver arguments, `x0 = AT_HWCAP | _IFUNC_ARG_HWCAP` and `x1` pointing at an
`__ifunc_arg_t` (`_size` 24, `_hwcap`, `_hwcap2`) in its frame, so a resolver
that dispatches on hwcap, function multiversioning included, sees the real CPU.
The loader calls a preinit entry with `(argc, argv, envp)`, as the static stub
calls its constructors, and the runner reads `AT_HWCAP` and `AT_HWCAP2` from the
auxiliary vector past `envp`'s terminator. An undefined IFUNC, or one outside
code, stays refused. The C frontend's own `__attribute__((ifunc("resolver")))`
writes the same symbol (an indirect `IrSymbolAlias` becomes
`ObjectSymbol.indirect` at the resolver's address), and is refused by name for
COFF and Mach-O output; see [linkage](agents/frontend/linkage.md).
`compiler_driver_elf_ifunc_tests` checks single resolution and address identity
against the host linker for static, dynamic, `-pie` and `-shared` images, and a
host-linked executable whose preinit callback calls a `-shared` library's IFUNC.
It also compiles the same C with `ide cc`, at `-O0` and `-O2`, and with a
local (`static`) ifunc, and runs each image against the host-built oracle. On
AArch64 it checks the hwcap arguments against the program's auxiliary vector.
`.ctors`/`.dtors` and `.init`/`.fini` inputs stay refused: GNU ld 2.42 folds
`.ctors` into `.init_array` in reverse but emits a null `.fini_array` slot for
`.dtors`, and lld 18 runs neither, so no consistent oracle exists, and the
fragments need the crti/crtn prologue Buster's entry stub does not link.

The Mach-O and COFF
readers refuse the same definitions in their own forms: an external `N_ABS` or
`N_INDR` symbol or a Mach-O common (`N_UNDF` with a size), and an external
`IMAGE_SYM_ABSOLUTE` symbol or a COFF common (section 0 with a size). Local
absolutes such as COFF `@comp.id`/`@feat.00` remain ignored producer metadata
(`compiler_driver_archive_test_foreign_reserved_symbols`).

These failures return `OBJECT_ERROR_UNSUPPORTED_TARGET` with a diagnostic naming
the section or symbol and its numeric type/index. Reserved symbol section
indexes additionally name `SHN_ABS`, `SHN_COMMON`, or `SHN_XINDEX` when known;
an empty symbol name appears as `<unnamed>`. Unknown reserved indexes keep
their numeric description. The registered archive diagnostic controls cover
both ELF architectures, selected and unused members, structured driver records
and output preservation. The driver includes the input
path, or archive/member path, in the import error and publishes no output image.
`object_test_elf_semantic_refusals` uses independent raw ELF records on both
architectures. `compiler_driver_elf_semantic_tests` imports host-compiled inputs
on Linux x86-64/AArch64, checks attributable refusal and no artifact, and requires
the host linker/runtime to preserve each input's meaning. A same-image
preinit/constructor control continues to link and run through both linkers, as
does a canonical optional GNU property control. `compiler_driver_elf_preinit_tests`
covers a preinit entry against a dependency's `constructor(0)` in both link
orders, against a host-built shared library's constructor in a dynamic
executable, the data-only sections above and the shared-output refusal, against
the host linker; `link_test` and `object_test` cover the merge order and the
writer mappings. Raw note controls cover every known feature combination,
unknown/required properties, malformed shape, and payload bounds;
`object_test_elf_property_note_walk` adds multi-note and `*_USED` sections and
truncated or overlong `descsz`/`datasz` values on both architectures.

## The work ledger: `ObjectWriteStatistics`

Every `ObjectArtifact` carries `ObjectWriteStatistics`, counted where the work
happens rather than estimated:

| Field | Meaning |
|---|---|
| `section_visits`, `symbol_visits`, `relocation_visits` | Loop iterations over each input array, validation included. Visits over count is the number of passes. |
| `image_bytes_reserved` | Bytes requested for the output image. |
| `image_bytes_stored` | Every store into the image, rewrites counted again. |
| `image_bytes_zeroed` | Stores that wrote zero fill or padding. |
| `image_bytes_patched` | Stores over bytes already stored. |
| `payload_bytes_copied` | Section payload bytes copied into the image. |
| `payload_bytes_borrowed` | Section payload bytes a borrowing write named in place instead of storing. |
| `scratch_bytes` | Other arena bytes the writer requested, released or not. |
| `retained_bytes` | What the call left allocated in the caller's arena, image included. |
| `output_bytes` | The artifact's length, or zero on error. |

For the planned ELF writer, `image_bytes_reserved`, `image_bytes_stored`,
`retained_bytes` and `output_bytes` are equal, `image_bytes_patched` is zero,
and each relocation and symbol is visited three times (validation, plan,
emission), priority split or not. The registered tests hold the writer to
those equalities. A borrowing write stores `image_bytes_reserved` minus
`payload_bytes_borrowed` bytes, still patches none, and retains its borrowed
table (one `ObjectBorrowedPayload` per payload) beyond the image; its
`payload_bytes_copied` plus `payload_bytes_borrowed` is what `object_write`
copies.

`ide cc -v -c` prints the ledger as one `OBJECT_WRITE` record, summed over the
objects when there are several inputs (`object_write_statistics_add`). For the
unity compiler at `-g0` (`ide cc -Isrc -Ibuild/generated -DBUSTER_UNITY_BUILD=1
-DBUSTER_INCLUDE_TESTS=0 -g0 -v -c src/buster/apps/ide/ide.c`), every
non-empty payload is at least 4 KiB, so all of them are borrowed. This is a
historical pre-compaction measurement, not the current output size:

```text
OBJECT_WRITE format=elf64 section_visits=150 symbol_visits=62646 relocation_visits=191454 image_reserved=38328904 image_stored=2509361 image_zeroed=107 image_patched=0 payload_copied=0 payload_borrowed=35819543 scratch=85312 retained=38329000 output=38328904
```

## COFF and Mach-O

`object_write_coff` and `object_write_mach_o64` still append into an
overestimated `ObjectBuffer`, zero-fill fixed records and patch their fields,
and scan every relocation once per section. They are counted through the same
statistics. COFF and Mach-O relocations also carry their addends in the
section bytes, so those payload stores are real patches that the format makes
necessary. Moving them onto a plan is a staged follow-up.

The append writers check their conservative reservation sum before reading
payloads or allocating an image. A reservation above `UINT32_MAX` is refused
with `OBJECT_ERROR_CAPACITY`, even when a tighter future plan could fit the
serialized bytes. Image and scratch tables must also fit the caller's arena.
All narrowed file offsets, table sizes and COFF symbol values use checked
32-bit conversion. Mach-O virtual section placement uses checked alignment
and addition; any emission failure returns no bytes and releases allocations.

Regular COFF refuses priority splitting past 65,279 sections; it does not
emit bigobj. An original section size above 32 bits is rejected before the
initializer entry census. Long section names use slash plus an ASCII decimal
string-table offset in the eight-byte Name field, so offsets above 9,999,999
are refused rather than truncated. The registered
`object_test_32_writer_limits` covers both architectures at the section limit,
fictitious oversized payload/name lengths without reading them, reservation
boundaries, arena refusal, zero-fill extents and virtual-address overflow.

## Shared relocation field facts

`object_relocation_properties` owns field width and TLS classification for
every `ObjectRelocationKind`. `object_relocation_kind_width` reports two bytes
for `COFF_SECTION16`, eight for the 64-bit data forms, and four for the other
fields. `object_relocation_kind_is_tls` includes the ELF, PE and Mach-O TLV
families. Invalid kinds report zero width and false TLS. The table's row count
is checked against the enum, and registered named golden rows independently
check every kind once.

The printer, codegen object conversion and object writer use the same
width. JIT and UEFI consume the same TLS classification and keep
their own supported-kind routing; UEFI refuses Mach-O TLV requests as
unsupported TLS before producing an image. Registered tests check a COFF
SECTION relocation in the final two payload bytes, its one-byte-short
refusal, and all three Mach-O TLV refusals through JIT and both UEFI targets.

## Validation

```sh
./build.sh build --config Release -t test_all
./build.sh test_self_host --config Release
./build.sh test_mode_matrix --config Release
```

Independent readers and linkers (GNU `readelf`, `llvm-readelf`,
`llvm-objdump`, `ld -r`, `ld.lld -r`, and full GNU ld/LLD links) are exercised
by the evidence script recorded with the introducing audit.

## In-memory executable sections

The object layer has no in-memory loader of its own. Tests that run an
`ObjectFile` in-process link it with `jit_link_object` and release it with
`jit_program_release` ([JIT](../src/buster/lib/compiler/jit/jit.h)), which owns
W^X finalization, call thunks and explicit host bindings. The JIT admits only
the host target, so object tests retarget a host-architecture fixture to
`target_native` before linking it. Relocation rules for foreign targets are
checked through the shared patch helpers (`object_aarch64_elf_page_relocate`,
`a64_page_relocation_apply`) against a modeled image layout instead.

The registered `object_test_executable_sections` checks data and BSS through
PC-relative and absolute references on x86-64/AArch64, repeated updates,
the JIT's refusal of over-page section alignment, and Linux, Windows and
macOS mapping permission queries.

## AArch64 textual assembly preservation

Issue #1280 requires each printed instruction to retain every field that
distinguishes its original 32-bit encoding. A word outside the printer's exact
mnemonic subset may use the existing `.word 0x...` spelling. Expanding the
mnemonic subset is optional; changing an instruction's bytes is not.

The registered `object_test_aarch64_printer_fields` fixture fixes 92 literal
instruction words and their expected mnemonic or raw-word lines across Linux,
Android, Windows and macOS AArch64 targets. The neighboring encodings cover
system operations, literal and unsigned loads, FP/vector memory, pairs, shifts,
register 31, reserved widths and immediate-shift bits without consulting the
production decoder. Seven literal-load cases retain their original self-targeting
word after an independent fixed `B +4` prefix; that branch establishes the target
label without a production decoder or relocation oracle.

The registered `compiler_driver_aarch64_printer_roundtrip` independently
assembles the original literal words and the printed assembly, extracts
`.text` directly from their ELF bytes and compares both with the fixed original
bytes (99 words including the seven label seeds). It also compiles a C corpus through both `-c` and `-S` for all four
allocators and both frontend forms, then assembles the original `-S` output
and compares its `.text` with the original `-c` output. The corpus exercises
fences, compare-exchange failure, signed extensions, HFA calls, scalar and vector
FP memory, and variadic FP retrieval. Linux and macOS require an available
Clang or native AArch64 GNU assembler observer; other desktop hosts report
observer unavailability explicitly. Mobile lanes retain the literal field
fixture.

These are regression oracles. Their presence alone does not establish that the
current printer satisfies the contract; qualification is tied to an actual
reviewed source head and hosted results. Observer admission stops after a failed
group cleanup, retained reservation or lost ownership.

The printer restricts system-register and bit-field aliases to their exact
supported encodings, and declines ignored SIMD/opcode/shift/reserved-width
fields. GPR data operands spell register 31 as ZR; memory and immediate-add
base operands retain SP/WSP. GPR pairs with non-temporal modes or assembler-
refused register overlaps also stay raw. The separate
`object_test_aarch64_printer_boundaries` pins nine original words for WSP,
nonzero immediate shifts, CSET-to-ZR and pair-overlap neighbors. Its original
44-byte host assembly control includes those words after NOP/RET; the original
92 cases and 99-word causal corpus stay unchanged.

The bounded partial #1281 repair suppresses type, label and size emission only
for the private local zero-value, zero-size FUNCTION anchor named `.text` in
the default AArch64 ELF text section. It leaves every relocation reference
intact: the independent assembler owns that section-base symbol. Public,
ordinary, undefined, nondefault-section, differently attributed and other-
target symbols retain their prior emission. The registered
`object_test_aarch64_text_anchor` fixes these positive and negative boundaries.

The host anchor control first assembles its original literal source, then
assembles the printer's unmodified output. Its independent ELF symbol/RELA
reader requires a local text SECTION symbol, the original ordinary local
zero-size FUNCTION, a public 44-byte FUNCTION, and a public 16-byte data OBJECT.
Two fixed AArch64 ABS64 relocations at offsets 0 and 8 retain addend 4 and
their section-base/public-function identities. The same original text bytes
must survive both paths. This control does not use the production object
reader, writer or symbol planner as its metadata oracle.

#1281 remains open: this slice does not qualify the AArch64 debug-anchor,
PLT/TLS or own-assembler rows. The x86-64 ELF listing rows (weak and hidden
binding, constructor priority, PLT calls, section-symbol anchors, TLS
general-dynamic padding, and the `-S -masm=att` refusal) are described in
[the driver guide](agents/driver.md) and qualified by their own tests. Actual qualification still requires source review
and fresh hosted results at the published repair head.
