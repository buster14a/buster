# object-writer

Run commands and resolve source paths from the repository root. Follow the
[shared agent rules](../../AGENTS.md), including the 9700X lock and `cores`
requirements. [Command index](README.md).

## Module overview

- `src/buster/compiler/object_writer.c` is the object writer: an ELF
  relocatable object per unit of the representation (`object_prepare` once per
  link, then `object_build` per object, `object_write_file`, and
  `object_write_units` for a group of lanes), with the DWARF of a unit's
  functions where `ObjectOptions.debug`'s entry for the unit gives it (#395:
  an `ObjectDebug` per unit, debug.c's sections and the relocations a linker
  resolves). `object_writer_test.c`
  holds its tests and `object_debug_test.c` those of its DWARF, which need
  debug.c's units (`main.c` includes both; `./build/compiler
  test-object-writer` runs them), and `object_writer_benchmark.c` provides
  `benchmark-object-writer`.
## Object writer tests

`./build/compiler test-object-writer [--seed N] [--cross-check] [--keep]` checks
`object_writer.c`:

- objects: each unit's object of `test-link`'s seven programs and two shared
  libraries (with 500 workers in the one that takes `--functions`), of a link
  made by hand (gaps between atoms, an atom of no size, weak, hidden and
  protected symbols, pointers into a string pool, a call to and a pointer to a
  local IFUNC, units without atoms between and after the others) and of a link
  without atoms, with and without function sections.
  Each is built twice, the second time over poisoned memory, and must come out
  the same; the test's own ELF reader reads it back and checks the rules every
  object keeps (`ELFOSABI_GNU` exactly when a symbol is an `STT_GNU_IFUNC` or
  a section has `SHF_GNU_RETAIN`, as the named sections' descriptors give a
  third of `.data`'s named atoms);
  and it is compared with the representation, from which the test works out
  every section, symbol and relocation again, with its own tables of section
  formats and relocation types. Among them, every local-exec TLS reference
  must be an `R_X86_64_TPOFF32` against its TLS symbol over a zero field, every
  call through the GOT an `R_X86_64_GOTPCRELX`, and every reference to a local
  IFUNC against the IFUNC. Then an arena too small for an object
  (`OBJECT_ERROR_FULL`), an addend past int64 (`OBJECT_ERROR_RANGE`), and links
  with an absolute symbol, referenced and not, which `object_prepare` refuses;
- debug (`object_debug_test.c`, #395): each unit of those sets and of the link
  made by hand with a debug.c unit of its functions (`ObjectOptions.debug`),
  shuffled, as one unit and as two, finalized at random addresses, with and
  without function sections, checked as above and against what the test
  works out again from the descriptor: the five sections after the sections
  of atoms, their bytes with the fields and address slots zero, each field an
  `R_X86_64_32` against its section's symbol with what it held, and each
  function's slot and line address an `R_X86_64_64` against its section's
  symbol at its offset; the descriptor's bytes as they were; and 50 changed
  descriptors (among them headers that aren't debug.c's, fields in their
  sections' bounds but not at their units' own contributions, and fields with
  the right values in the wrong places), and an IFUNC's,
  which `object_build` refuses
  (`OBJECT_ERROR_DEBUG`) with nothing on the arena, as it refuses another
  unit's descriptor as a unit's entry and entries one fewer than the units;
  then every unit's object of the lanes' program below, each with its own
  entry (every fourth empty: no DWARF, the same bytes as without), by
  `object_write_units` on 1, 2, 3 and 8 lanes into `object_build`'s bytes,
  and with entries one fewer, which it refuses for every unit, writing none;
- many sections: one unit of 33,000 functions in sections of their own, 66,041
  sections: `e_shnum` and `e_shstrndx` in section 0, and symbols' indices
  through `SHN_XINDEX` and `.symtab_shndx`;
- lanes: every unit's object of a program of 24 units and a 25th without
  atoms written by `object_write_units` with groups of 1, 2, 3 and 8 lanes,
  byte-identical to `object_build`'s; and a group in which a lane has no room
  to start, which writes no object and gives every unit `OBJECT_ERROR_FULL`;
- files: `object_write_file` into a new file, over a larger and a smaller one
  (in place: the same inode, truncated), over one with another hard link (a
  new file with its mode, and the other link keeps the old bytes), also with a
  name of `NAME_MAX` bytes, into `/dev/null`, into a pipe, and into a missing
  directory (an error);
- with `--cross-check`, which needs `readelf`, `llvm-readobj`, `as`,
  `ld.bfd`, `ld.lld`, `mold` and `wild` on `PATH`, and a `cc` that takes
  `-fuse-ld=wild` (GCC 16 on the 9700X):
  - `readelf -W` and `llvm-readobj` list every object's OS ABI, sections,
    symbols and relocations, which must be what the test's reader read, field
    by field;
  - GNU ld, lld, mold and wild link each program's objects, without errors or
    warnings: the static programs with `-static -e _start`, the ones that
    call libc through `cc` as a PIE, with `-no-pie` and with `-static`, and
    the shared libraries through `cc -shared`, each loaded by a harness that
    `cc` builds, which calls its `buster_library_main` with `main`'s
    arguments (`dlopen` and `dlsym`). The executable (or the harness) must
    exit with status 0 having printed what the program's model predicts. wild links only the programs without `SIZE32` and `SIZE64`, which
    wild 0.10 refuses, and among them one of 33,000 functions past
    `SHN_LORESERVE` sections. The same linkers link, the same three ways, a
    program whose `main` calls a local IFUNC directly and through a pointer,
    which must exit with status 42, 21 from each call (calling the resolver
    itself would return its implementation's address);
  - GNU as assembles each unit as `test-link --print-assembly` writes it, and
    its object must match the writer's: the same sections and bytes, symbols
    and relocations. They may differ where GNU as does: it orders sections as
    the source names them and places them in the file its own way, keeps an
    empty `.text`, `.data` and `.bss`, adds a `.note.gnu.property` where
    binutils is built to (as on the 9700X), writes no `.comment` or
    `STT_FILE`, keeps only the unnamed symbols relocations into `SHF_MERGE`
    sections need, and for a `.reloc` against a named local symbol keeps the
    symbol where the writer, like GNU as for an instruction, names the
    section's. So a relocation's target is compared by where it points, but
    its form must be the same for the GOT, TLS and size types and into
    `SHF_MERGE` sections;
  - the objects with DWARF of the defaults and of many units (one debug unit
    for an even unit, two for an odd one), with and without function
    sections, listed by both tools and linked by GNU ld, lld and mold with
    `-static -e _start` into programs that print what they should, whose
    DWARF `llvm-dwarfdump --verify` passes and is the objects'
    contributions, relocated where each linker put them (needs
    `llvm-dwarfdump` too).

The objects and programs go in a new directory under `$TMPDIR` (or `/tmp`),
removed at the end unless `--keep`. `--seed N` changes the programs. On the
9700X (binutils 2.47, LLVM 23.1.1, mold 3.0.0, wild 0.10.0 and GCC 16.2.1)
the checks take about 0.04 s, and 3.7 s with `--cross-check`.

## Object writer benchmark

`./build/compiler benchmark-object-writer [--units N] [--functions N]
[--filler N] [--function-sections] [--directory DIR] [--seed N]` generates a
program of `--units` units (64 by default), each of `--functions` workers (200)
of up to `--filler` filler instructions (40), with 8 globals, 2 thread-locals,
20 strings and an init and a fini function per unit: by default each unit is
about one of synth's translation units. It reports, per object, 21-pass medians
of `object_prepare` (once per link), of `object_build` alone (with cycles,
instructions and IPC), and of writing each object to a file in `--directory`
(`$TMPDIR` or `/tmp`) over the one from the pass before (in place) and as a new
file, each against its floor: the same system calls without the writer's
`fstat`. Each way writes files of its own and the ways take turns going first,
since rewriting pages just written costs less. Run it pinned to one CPU, and
with `--directory` on each file system to measure;
`docs/object-writer-and-linker.md` ("The object writer (`-c`)") has the numbers
for tmpfs and btrfs. The default 9700X job runs it after the tests, pinned to
one allowed CPU; like the others, its measurements are informational.
