# Object emission: planned writing and its work ledger

`object_write` (`src/buster/lib/compiler/object/object.c`) serializes the
format-neutral `ObjectFile` into ELF64, COFF or Mach-O bytes. This guide states
the contract of the planned ELF64 writer, how every writer's work is counted,
and what remains to be moved onto the same plan. The measured history and the
staged redesign of the whole path, from machine code to the published file,
are in the object-emission design issue and the audit that introduced this
writer.

## ELF64: plan every range, then store each byte once

`object_write_elf64` runs in two phases over the object after the priority
split (`object_split_initializer_priorities`). The split appends one section
per constructor/destructor priority group and records where each grouped
initializer entry went; it does not copy the relocations. Both phases place
each relocation through `object_initializer_relocation_place` as they reach
it. The split's tables are sized by sections and entries, and a split past the
section limit or the arena is refused like the plan's own limits.

1. **Plan** (`object_elf64_plan`). One pass over the relocations counts each
   section's and the number of RELA tables; one pass over the symbols counts
   the locals and sizes `.strtab`; one pass over the sections assigns each name
   its `.shstrtab` offset and each payload its file offset after alignment
   padding. Every offset and size is a checked `u64` sum. The plan refuses,
   before any image exists:
   - a relocation kind with no ELF type (`OBJECT_ERROR_UNSUPPORTED_TARGET`);
   - a section count that reaches `SHN_LORESERVE` (`0xff00`), which `e_shnum`
     and `st_shndx` cannot state without extended numbering;
   - a `.strtab` or `.shstrtab` with a byte past a 32-bit offset;
   - an offset or size sum that overflows, or a file the arena cannot hold
     (all `OBJECT_ERROR_CAPACITY`).
2. **Emit** (`object_elf64_emit`). The image is exactly the planned size. Each
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

The bytes are identical to the writer this replaced. Test builds keep that
writer as `object_test_write_elf64_reference`, a differential oracle; the
registered `object_test_elf_planned_writer` compares both on seeded and
adversarial objects. Retire the oracle when an intended ELF output change
lands (for example [#1288](https://github.com/buster14a/buster/issues/1288)'s
empty-section removal), replacing byte comparison with a read-back comparison.

## ELF64 import semantics

`object_read_elf64` accepts allocated PROGBITS/NOBITS payloads, supported
unwind records and init/fini arrays. Existing preinit records are admitted into
the initializer model; `.preinit_array` names receive priority zero. The current
runtime control checks a preinit entry before a constructor in the same image.
It does not establish a distinct loader-facing preinit phase. The
[ELF initialization contract](https://gabi.xinuos.com/v42/elf/08-dynamic.html#initialization-and-termination-functions)
requires executable preinit entries to run before dependency constructors and
prohibits them in shared objects. That phase, section-type-independent ordering
and the shared-object prohibition remain open under
[#1243](https://github.com/buster14a/buster/issues/1243).

The bounded refusal repair for #1243 rejects unsupported allocated section
types, legacy `.ctors`/`.dtors` and their priority families, `.init`/`.fini`
fragments, and exception tables the reader previously discarded. An unsupported
allocated note is refused too; its contract must be understood before it can be dropped.
One canonical GNU property note is understood: the optional x86 IBT/SHSTK or
AArch64 BTI/PAC/GCS `FEATURE_1_AND` record. Its output feature intersection is
zero because Buster's generated code does not assert those features, so this
note is omitted. Unknown bits, additional properties, required ISA properties,
other note formats, and unsupported flags/alignment are refused.
Unallocated unknown metadata and unsupported debug section types retain their
skip policy. Supported DWARF payloads still pass through without DIE decoding.

Ordinary NOTYPE/OBJECT/FUNC/SECTION/TLS symbols keep their existing mapping.
STT_FILE records are metadata and may use SHN_ABS. Other reserved section
definitions (including absolute/common values and extended indexes) and
unsupported runtime symbol types, including GNU IFUNC, are refused. Calling an
IFUNC resolver as a normal function or dropping a weak absolute definition
would produce a successful link with different behavior.

These failures return `OBJECT_ERROR_UNSUPPORTED_TARGET` with a diagnostic naming
the section or symbol and its numeric type/index. The driver includes the input
path, or archive/member path, in the import error and publishes no output image.
`object_test_elf_semantic_refusals` uses independent raw ELF records on both
architectures. `compiler_driver_elf_semantic_tests` imports host-compiled inputs
on Linux x86-64/AArch64, checks attributable refusal and no artifact, and requires
the host linker/runtime to preserve each input's meaning. A same-image
preinit/constructor control continues to link and run through both linkers, as
does a canonical optional GNU property control. It does not cover dependency
constructor ordering, preinit section types with other names, or preinit in a
shared output. Raw note controls cover every known feature combination,
unknown/required properties, malformed shape, and payload bounds.

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
non-empty payload is at least 4 KiB, so all of them are borrowed:

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

`object_link_executable` relocates an object inside one temporary writable,
nonexecutable reservation, then publishes it only after final protection and
instruction-cache flushing succeed. Each nonempty section occupies its own
page-rounded span: text becomes RX, mutable data and zero-fill become RW, and
readonly/unwind/initializer/debug sections become R. No final page is both
writable and executable. Section alignments greater than a host page remain
absolute address constraints; padding stays readonly.

`ObjectExecutable.address` names the first nonempty text section. Its
`allocation_address` and `allocation_size` name the complete reservation,
including any alignment prefix, and `object_release_executable` releases that
reservation. Failed layout, relocation, protection or cache flushing publishes
neither address. This helper does not register unwind tables or run constructors,
and thread-local relocations still require an external runtime and are refused.

The registered `object_test_executable_sections` checks data and BSS through
PC-relative and absolute references on x86-64/AArch64, repeated updates,
over-page alignment and Linux, Windows and macOS mapping permission queries.

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

#1281 remains open: this slice does not qualify its x86/debug-anchor,
constructor priority, weak/hidden binding, PLT/TLS, assembly-dialect or own-
assembler acceptance rows. Actual qualification still requires source review
and fresh hosted results at the published repair head.
