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
split (`object_split_initializer_priorities`).

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

The bytes are identical to the writer this replaced. Test builds keep that
writer as `object_test_write_elf64_reference`, a differential oracle; the
registered `object_test_elf_planned_writer` compares both on seeded and
adversarial objects. Retire the oracle when an intended ELF output change
lands (for example [#1288](https://github.com/buster14a/buster/issues/1288)'s
empty-section removal), replacing byte comparison with a read-back comparison.

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
| `scratch_bytes` | Other arena bytes the writer requested, released or not. |
| `retained_bytes` | What the call left allocated in the caller's arena, image included. |
| `output_bytes` | The artifact's length, or zero on error. |

For the planned ELF writer, `image_bytes_reserved`, `image_bytes_stored`,
`retained_bytes` and `output_bytes` are equal, `image_bytes_patched` is zero,
and each relocation and symbol is visited three times (validation, plan,
emission), plus once more per relocation when a priority split happens. The
registered tests hold the writer to those equalities.

`ide cc -v -c` prints the ledger as one `OBJECT_WRITE` record:

```text
OBJECT_WRITE format=elf64 section_visits=126 symbol_visits=61944 relocation_visits=187695 image_reserved=38057760 image_stored=38057760 image_zeroed=113 image_patched=0 payload_copied=35589769 scratch=84280 retained=38057760 output=38057760
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
