# The object writer and the linker

This is the design of the two components that turn the compiler's encoded
machine code and data into files:

- **The object writer:** ET_REL objects, for `-c` and for other linkers.
- **The linker:** executables written straight from the compiler, and links of
  other compilers' objects, archives and shared libraries.

The lane base layer, the encoder's fixups, the representation and the direct
path's first executables are implemented (`src/buster/compiler/lane.c`,
`x86_64_encoder.c`, `link.c` with generated test programs in
`link_program.c`, and `executable.c`); the object writer and the rest of the
linker aren't yet. The document records the measurements on the Ryzen 7 9700X
that the design rests on, the design itself, and the order to build it in, as
`docs/x86-64-encoder.md` does for the encoder.

The premise is that most of what mold and wild do on every link is work this
compiler can avoid altogether. What remains is memory traffic, and the design
can bring that close to what the kernel needs to put a file in the page cache.

## Summary

- **The fastest link is the one that doesn't happen.** Without `-c`, one
  command compiles and links (`cc main.c -o main`), and the compiler:
  - binds every name while it compiles: its own definitions directly, the C
    library's through the GOT;
  - lays the image out from the sizes the encoder returns;
  - patches the references it couldn't fill in;
  - writes the executable once.

  No object file is written or read, no name is hashed at link time, and no
  instruction is relaxed afterwards.
- **The C runtime is read once, not on every link.** The crt objects, libc and
  libgcc are the same for every link, and reading them is most of what a small
  link costs mold and wild (0.59 of wild's 1.09 ms on hello world go to
  opening them and reading their symbols). The compiler digests them once into
  a *runtime snapshot*, a small cache file checked on each link with one `stat`
  per file.
- **One representation serves every path.** Atoms, symbols, fixups and names,
  each a column array indexed by a 32-bit id, carry the direct path, the object
  writer and imported ELF inputs alike.
- **Every phase is written in lanes.** This is Ryan Fleury's *multi-core by
  default*, the style of the RAD Debugger's base layer:
  - every thread runs the same code, taking parts of the work, cut by cost,
    from a counter, and meeting at `lane_sync`;
  - each lane writes only what it owns;
  - names are resolved by exchanging records through fixed hash partitions,
    with no shared hash table and no locks.

  One lane is the first implementation, so the barriers return at once.
- **SIMD goes where the 9700X shows it pays.** Patching relocations and writing
  ELF records are bound by loads and stores, and scalar code ties AVX-512 on
  them, so they stay scalar. Hashing names, splitting strings and decoding
  relocation records are vector work, and so is the encoder, which already
  produces most of the output's bytes.

What it targets, as link work after code generation with one lane, against the
time mold and wild take in total. Their times are medians on the 9700X, with
the output on tmpfs:

| Program (output) | mold, 1 thread / 15 threads | wild, 1 / 15 | Direct path target, 1 lane | Factor (vs wild at 15 threads) |
| --- | ---: | ---: | ---: | ---: |
| hello world (6–16 KB) | 2.43 / 5.35 ms | 1.27 / 2.58 ms | ≤ 20 µs | ≥ 125× |
| tcc (440 KB) | 3.30 / 6.75 ms | 2.14 / 2.95 ms | 50–120 µs | 25–60× |
| 102,400 functions (46 MB) | 96 / 38 ms | 140 / 25 ms | 4–10 ms | 2.5–6× |

Step 4's static executables without libc, written by one lane from generated
programs of 3.9 KB, 397 KB and 35.5 MB, take 2.7 µs, 70 µs and 8.5 ms in
place on tmpfs (3.4 µs, 87 µs and 9.9 ms as new files), and the largest
4.1 ms with eight lanes ("The first executables"). A dynamic PIE that calls
libc has more to write (step 5).

Orders of magnitude are there for programs up to a few megabytes, where
fixed costs dominate. For a 46 MB output both sides spend their time in the
page cache, and the gap closes to what one lane against fifteen threads
allows. "What 'orders of magnitude' can mean" covers the ways past that.

## What a link costs today

### The inputs

Three programs, each linked as the clang driver on this Arch system links it
(dynamic PIE against glibc 2.44, with `--build-id` and `--eh-frame-hdr`):

- **hello world**, and the same with `-static`;
- **tcc**: tinycc at 43c7708b, `tcc.o` plus `libtcc.a` (10 objects) and `-lm
  -ldl -lpthread`, built with `-O2` and again with `-O2 -g`;
- **synth**: a generated program of 512 translation units with 200 functions
  each, compiled with `clang -O2`, and again with `-O2 -g`.

In synth, every function calls three others across files and reads and writes
globals in other files, and every file has a function-pointer table and string
literals. That gives 35 MB of `.text` with 1.02 million relocations: one per
34 bytes of code, the same as tcc's objects (32–37 bytes in `tccgen.o`,
`tccpp.o`, `libtcc.o` and `x86_64-gen.o`).

The relocation types are the same three, in somewhat different proportions
(all of an object's relocations, `.eh_frame`'s included):

| Objects | PC32 | PLT32 | REX_GOTPCRELX |
| --- | ---: | ---: | ---: |
| synth's | 36% | 45% | 18% |
| `tccgen.o` | 48% | 29% | 22% |

Every linker's output runs and prints what the program should.

### Baselines

Wall time as the caller sees it, from a fork-and-exec harness: the median of 41
runs, or 11 for synth. The linkers run with their defaults: 15 threads, and mold
and wild fork so that the caller doesn't wait for their teardown. The output
file exists from the previous run, on tmpfs. All times are milliseconds.

| Program (output) | GNU ld 2.47 | lld 23.1 | mold 3.0 | wild 0.10 |
| --- | ---: | ---: | ---: | ---: |
| hello (6–16 KB) | 6.14 | 5.97 | 5.35 | 2.58 |
| hello, `-static` (815–865 KB) | 14.15 | 8.90 | 8.55 | 3.92 |
| tcc (440 KB) | 10.46 | 6.94 | 6.75 | 2.95 |
| tcc, `-g` (1.4 MB) | 12.54 | 7.72 | 7.90 | 3.17 |
| synth (46 MB) | 344.1 | 58.6 | 37.9 | 25.4 |
| synth, `-g` (126 MB) | 473.0 | 74.5 | 50.5 | 41.4 |

Two variants of the same measurement:

- **One thread** (`--threads=1` or `--no-threads`), pinned to CPU 2. mold and
  wild are 1.4–2.2× *faster* on small programs this way, because starting 15
  threads costs more than the link itself.
- **Output on btrfs** (the machine's `/home`, with zstd compression).

| Program | lld, 1 thread | mold, 1 thread | wild, 1 thread | lld, btrfs | mold, btrfs | wild, btrfs |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| hello | 5.05 | 2.43 | 1.27 | 6.06 | 5.50 | 2.53 |
| tcc | 5.87 | 3.30 | 2.14 | 7.00 | 7.13 | 2.97 |
| synth | 105.6 | 96.4 | 139.9 | 65.6 | 66.1 | 34.3 |
| synth, `-g` | 161.7 | 143.5 | 233.3 | 90.1 | 106.7 | 68.5 |

**Matching the options.** The direct path's executables bind at load time and
keep every section, and wild's guide recommends giving every linker the same
options. With `-z now --no-gc-sections` added to every link above, no median
moved by more than 5%:

| Run | Default options | Matched options |
| --- | ---: | ---: |
| hello, mold | 5.35 | 5.32 |
| hello, wild | 2.58 | 2.52 |
| synth, mold | 37.9 | 38.0 |
| synth, wild | 25.4 | 25.9 |
| hello, mold, one thread | 2.43 | 2.44 |
| hello, wild, one thread | 1.27 | 1.34 |

The full table is in the appendix, with the exact commands.

For scale, tinycc, a compiler that links its own output, takes:

- 1.03 ms to compile and link hello world;
- 0.66 ms to compile it alone (`-c`);
- 0.56 ms to link the object.

A process that does nothing costs 67 µs (static) or 105 µs (dynamic, glibc) to
fork, exec and exit.

### Where the time goes

**Small links are fixed costs.**

- Under `perf stat` with one thread, mold runs 23.5 million instructions and
  takes 829 page faults to write a hello world of a few kilobytes. wild runs
  10.3 million instructions and takes 696 faults.
- wild's `--time` breaks its 1.09 ms down:
  - 0.22 ms opening inputs;
  - 0.37 ms loading symbols into its symbol database (0.18 ms reading
    symbols, 0.14 ms populating the map);
  - 0.20 ms layout;
  - 0.13 ms writing;
  - 0.06 ms shutdown.

  Nearly all of the symbols are libc.so.6's 3,170 definitions, libgcc's and
  the crt objects'. `hello.o` adds two: `main`, and a reference to `puts`.

**Large links are memory traffic.** With one thread, mold links synth in 96 ms:
0.47 GB/s of output, 1.05 billion instructions, about 1,000 per relocation.
Its `--perf` breakdown (ms):

| Phase | Time | Within it |
| --- | ---: | --- |
| `read_input_files` | 16 | |
| `before_copy` | 45 | `resolve_symbols` 19, `scan_relocations` 7, merged sections 6 |
| `copy` | 35 | `.text` 19, `.symtab` 8 |
| build ID | 5 | |

wild takes 140 ms with one thread (ms):

| Phase | Time | Within it |
| --- | ---: | --- |
| loading the symbol database | 10.5 | |
| symbol resolution | 21.4 | |
| layout | 41 | finding required sections 28.7 |
| writing | 64.7 | writing data 58 |

The mold paper (arXiv 2608.23228) reports the same shape at scale:

- **One thread:** mold links Firefox's debug build (2.47 GiB) in 12.2 s
  (§7.3, Table 7).
- **One pass on one thread:** its ablation runs each pass on one thread while
  the others stay parallel (§7.4, Table 8). Copying and applying relocations
  then take 5.18 s, about 0.5 GB/s, against 0.24 s in parallel.

Its multi-thread speedups come from running every pass in parallel over
homogeneous arrays, with copying fused with relocation (§3, §4.10).

## The floors

What the machine and the kernel charge, measured on the 9700X with one thread
pinned to CPU 2 (medians of 5–15 runs).

**Creating the output file** (µs; GB/s in parentheses):

| Size | tmpfs, new file | tmpfs, in place | tmpfs, new, `mmap` | btrfs, new file | btrfs, `O_TRUNC` | btrfs, in place |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 16 KB | 3.7 | 1.1 | 5.5 | 12.0 | 57.8 | 3.3 |
| 256 KB | 21.5 (12.2) | 6.3 (41.6) | 39.4 | 59.3 (4.4) | 220.5 | 40.5 (6.5) |
| 4 MB | 333 (12.6) | 108 (38.7) | 589 (7.1) | 562 (7.5) | 10,403 | 446 (9.4) |
| 64 MB | 9,665 (6.9) | 4,811 (14.0) | 13,804 (4.9) | 12,920 (5.2) | 19,509 | 10,438 (6.4) |
| 512 MB | 79,283 (6.8) | 45,731 (11.7) | 109,829 (4.9) | 107,366 (5.0) | 177,037 | 83,864 (6.4) |

How each method writes:

| Method | Steps |
| --- | --- |
| New file | `unlink`, `open(O_CREAT)`, `pwrite` and `close` |
| In place | `pwrite` over an existing file of the same size |
| `mmap` | `ftruncate`, `MAP_SHARED`, `memcpy` and `munmap` |

Writing to a temporary file and renaming it costs a little more than a new
file: 4.1 µs against 3.7 at 16 KB on tmpfs, 20.4 against 12.0 on btrfs.

`fallocate` with `MADV_HUGEPAGE` was slower than plain `mmap` on tmpfs: 4.2
GB/s against 4.9 at 64 MB with one thread, and 6.5 against 14.1 at 512 MB with
eight. On btrfs it was within 5%. mold measured each of them making its copy
pass about 3× faster on ext4 (§5.1–5.2), so this choice depends on the
filesystem.

**Writing one file from several threads** (each thread writes a contiguous
slice; GB/s):

| Size and filesystem | Method | 1 thread | 2 | 4 | 8 |
| --- | --- | ---: | ---: | ---: | ---: |
| 512 MB, tmpfs | `pwrite`, new file | 8.6 | 8.6 | 8.5 | 8.4 |
| | `pwrite`, in place | 11.7 | 11.8 | 11.7 | 11.7 |
| | `mmap`, new file | 5.4 | 8.8 | 12.4 | 14.1 |
| | `mmap`, in place | 8.0 | 12.7 | 17.6 | 19.1 |
| 64 MB, btrfs | `pwrite`, new file | 4.9 | 4.9 | 5.0 | 5.1 |
| | `mmap`, new file | 3.4 | 4.9 | 5.1 | 4.3 |

`pwrite` doesn't scale: buffered writes to one file are serialized in the
kernel. `mmap` does scale on tmpfs, through page faults on separate pages, but
not on btrfs.

**Memory:**

- **Anonymous memory, first touch:** 463 ns per 4K page (8.9 GB/s), or 168 ns
  per 4K with transparent huge pages (24 GB/s).
- **Teardown:** releasing 1 GB of touched memory at exit takes 11.3 ms with 4K
  pages and 1.06 ms with huge pages; 4 GB takes 48 ms and 4.2 ms. So arenas
  use `MADV_HUGEPAGE`. With it, the fork that mold and wild use to hide
  teardown from the caller isn't worth having at the sizes this compiler
  reaches.
- **Copying:**
  - `memcpy` runs at 86–92 GB/s within L2 and L3 (up to 16 MB) and 21 GB/s
    from DRAM (256 MB);
  - non-temporal stores give 25.6–27.1 GB/s at every size;
  - `memset` runs at 264 GB/s in L2.

**Small calls a linker makes per input file:**

| Call | Time |
| --- | ---: |
| `open` and `close` | 0.42 µs |
| `stat` | 0.18 µs |
| `mmap` and `munmap` without touching the pages | 0.64 µs |
| mapping libc.so.6 and touching all 541 of its pages | 8.2 µs (15 ns per page, with fault-around) |
| `pread` of the 2.2 MB file | 28 µs (79 GB/s) |

**ETXTBSY.** Opening a running executable for writing still fails with ETXTBSY
on both kernels here (6.18 and 7.2), and so does `execve` of a file that is
open for writing.

## What "orders of magnitude" can mean

There are four separate claims, and each optimization below has to say which
bytes stop moving, which records stop being visited, or which dependency chain
gets shorter:

| Path | Work measured |
| --- | --- |
| Direct (no `-c`) | the compiler's encoded output → a finished executable |
| Object writer (`-c`) | the same state → an ET_REL file |
| Full link | identical ELF objects, archives and shared libraries → an executable |
| Incremental | a previous output plus the changed functions → the new executable |

**Direct path, small programs.** A hello world's link should cost about as much
as its output file: 1–4 µs on tmpfs, 3–12 µs on btrfs. On top of that:

- a dozen `stat` calls, about 2 µs, to check the runtime snapshot;
- reading the snapshot, a few microseconds;
- layout, patching and writing the dynamic sections, a few microseconds more.

That is 130–270× faster than wild and mold at their defaults, and 64–120×
faster than their single-thread runs, because the work they repeat on every
link (reading libc.so.6 and libgcc, starting threads, a process) is gone.

For tcc's 440 KB:

- the file costs 11–36 µs on tmpfs (in place or new) and 60–100 µs on btrfs;
- its 12,000 fixups cost 5–7 µs (see "SIMD").

That is about 50–120 µs in all: 25–60× wild and 55–135× mold at their
defaults.

**Direct path, large programs.** Synth's 46 MB costs between:

- 3.3 ms, the floor for overwriting it in place on tmpfs (14 GB/s), and
- 8.8 ms, the floor for a new file on btrfs (5.2 GB/s),

plus 0.5–1.4 ms of patching (the more when the code no longer fits in L3).
That is 10–35× mold and wild with one thread, but only 2.5–9.5× their 15
threads: both sides are now bound by the page cache, which doesn't care who
writes. With eight lanes copying into a shared mapping, tmpfs takes 14–19 GB/s
(2.4–3.3 ms). btrfs stays near 5 GB/s whoever writes.

Getting past this means not writing those bytes at the end:

- **Streaming:** write them while compiling, which "Streaming the output"
  describes.
- **Incremental output:** don't write the ones that didn't change.

**Object writer.** It is bound by its file write, like the direct path. The
work around it is a few microseconds per object. Objects aren't on the direct
path's way, so their speed matters for `-c` builds and for other linkers.

**Full link of foreign objects.** This path has to read inputs it didn't
produce and hash names it didn't intern. mold's single thread manages about
0.5 GB/s of output. Vectorized parsing, partitioned resolution, fused
copy-and-patch and the write floors above put 2–5 GB/s per lane within reach.
That is a large factor, but not two orders of magnitude, and this document
doesn't claim them here.

## Design

### Representation

One representation serves everything. The encoder and the data emitter produce
it, the ELF importer converts foreign inputs into it, the object writer
serializes it, and the linker lays it out and patches it. The tables are column
arrays indexed by 32-bit ids, and id 0 of each kind means "none". Final
addresses and file offsets are 64-bit, so the ids don't put a 4 GiB limit on
the output. `src/buster/compiler/link.c` implements it, and its orientation
comment lists the rules its adds enforce.

**Names** (`NameId`) are spellings interned by the front end. Each has its
bytes in the name arena and a 64-bit hash computed once, when the name is
interned. Local labels, `static` functions and string literals never need
their name to be looked up: references carry ids, and their symbols can have
no name (`NameId` 0).

- **The bytes** are followed by a NUL, which the ELF string tables need, and
  the length is kept.
- **The hash** is `link_hash`: wyhash's mixing of 8 bytes at a time, scalar
  for now. From the runtime snapshot on it is part of the format, so the
  import path's vectorized hashing has to compute the same function, or
  replace it everywhere.
- **The table doesn't deduplicate:** the front end interns, and gives each
  spelling one id.

**Atoms** (`AtomId`) are what the linker places and keeps or drops as a whole:

- a function;
- a global object;
- a pool of string literals;
- a piece of a synthetic section;
- for foreign inputs, an input section, since nothing safely splits one
  further (aliases, section-relative references and shared constants cross the
  boundaries functions suggest).

Columns:

| Column | Contents |
| --- | --- |
| `class` | the output section it goes to (`BUSTER_LINK_CLASS_TEXT`, `_RODATA`, `_DATA`, `_BSS`, `_TDATA`, `_INIT_ARRAY`, ...) |
| `alignment_log2` | its alignment |
| `unit` | the translation unit or input it comes from: the object the object writer puts it in, and the scope of its local symbols |
| `size` | its size |
| `bytes` | its contents: in a lane's buffer, the runtime snapshot or a mapped input; null for zero-fill |
| fixup range | where its fixups are: a first `FixupId` and a count |
| `address` | after layout: an image offset for a loaded atom, a file offset otherwise |

The classes are numbered in lld's order of segments, so that sorting atoms by
class gives the layout's order: `_RODATA` and `_STRINGS` (pools of string
literals: `.rodata.str1.1` in an object), `_TEXT`, `_TDATA`, `_TBSS`,
`_INIT_ARRAY`, `_FINI_ARRAY`, `_DATA_RELRO`, `_DATA` and `_BSS`. `_TBSS` and
`_BSS` are zero-fill.

**Symbols** (`SymbolId`) are what binding is about. One atom can define
several, and several definitions can compete for one name. Columns:

| Column | Contents |
| --- | --- |
| `name` | its `NameId` |
| `atom`, `value` | the defining atom and the offset in it |
| `size` | its size |
| `flags` | binding, visibility, type, where ELF's `st_info` and `st_other` have them |
| `source` | where it is defined: program, runtime shared library, runtime archive member, foreign object, foreign shared library, or the linker |
| `got_slot`, `dynamic_index`, `version` | its GOT slot, `.dynsym` index and version |
| `reference` | after layout: what code that refers to it uses (its address, its GOT slot's for an import, its offset in the TLS block for a TLS variable) |

`reference` is the only symbol column the patch kernel reads. It is 32-bit,
because a small-code-model image (everything a PC32 field can reach) is under
2 GiB, and the measured 32-bit kernel is the fastest (see "SIMD").

**Fixups** are references the encoder couldn't fill in. They are sorted by
atom and offset: each atom's fixups are one range of consecutive ids, sorted
by offset, whose fields don't overlap, and the ranges of different atoms don't
interleave. 13 bytes each in columns:

| Column | Contents |
| --- | --- |
| `offset` | the field's offset in the atom (32-bit) |
| `target` | a `SymbolId` (32-bit) |
| `addend` | 32-bit, relative to the field |
| `kind` | 8-bit, from the table below |

A fixup keeps what the reference means: the symbol it names, never a GOT slot
or an offset worked out in advance, and a kind that says how the instruction or
the data reaches it. The object writer needs exactly that to pick an ELF
relocation, and an executable's patch kernel derives its arithmetic from it:

| Kind | Where | Relocation in an object | Value in an executable |
| --- | --- | --- | --- |
| `CALL` | a branch's rel32: `call`, `jmp` or `jcc` | `R_X86_64_PLT32` | S + A − P |
| `CALL_GOT` | `call` or `jmp [rip + slot]` (`ff 15`, `ff 25`) | `R_X86_64_GOTPCRELX` | G + A − P |
| `LOAD_GOT` | `mov r64, [rip + slot]` | `R_X86_64_REX_GOTPCRELX`: loading a pointer has REX.W | G + A − P, or, when S turns out to be local, S + A − P with `mov` rewritten to `lea` |
| `GOTPCREL` | any other instruction that reads the slot (`push [rip + slot]`) | `R_X86_64_GOTPCREL` | G + A − P, never rewritten |
| `PC32` | any other RIP-relative operand or rel32, or 4 bytes of data | `R_X86_64_PC32` | S + A − P, checked to fit in int32 |
| `TPOFF32` | a 32-bit displacement or immediate: `fs:[x@tpoff]` (local-exec), `[rax + x@tpoff]` | `R_X86_64_TPOFF32` | S's offset from the end of the TLS block + A |
| `GOTTPOFF` | `mov` or `add r64, [rip + x@gottpoff]` (initial-exec) | `R_X86_64_GOTTPOFF` | G + A − P, with a `TPOFF64` for the slot |
| `ABS64` | 8 bytes of data, or `movabs`'s imm64 | `R_X86_64_64` | S + A, and in a PIE a relative relocation (a symbolic one for an import) |
| `PC64` | 8 bytes of data (unwind tables) | `R_X86_64_PC64` | S + A − P |
| `ABS32`, `ABS32S` | 4 bytes: data, an imm32 or a disp32, non-PIE only | `R_X86_64_32`, `R_X86_64_32S` | S + A, zero- or sign-extension checked |
| `SIZE32`, `SIZE64` | 4 or 8 bytes: data, an imm32 or an imm64 | `R_X86_64_SIZE32`, `R_X86_64_SIZE64` | the size of S + A |

G is the address of S's GOT slot. A linker that relaxes `CALL_GOT`,
`LOAD_GOT` or `GOTTPOFF` rewrites the instruction's bytes before the field,
from what its relocation type says they are, so those kinds go only on those
instructions, without a segment override; any other read of a slot is
`GOTPCREL`, which linkers leave alone.

**Wide addends.** Any kind can carry a 64-bit addend: a bit in `kind` sends
the patch to a side table that holds it. Initializers like
`(char*)&x + 0x100000000` need it, and so do foreign `R_X86_64_PC64` and
`R_X86_64_64` relocations with large addends, which GNU as and ld accept.
The common case stays 13 bytes. The bit is the kind's top bit
(`BUSTER_LINK_FIXUP_WIDE`), and the `addend` column then holds the side
table's index. An addend goes there exactly when it doesn't fit in int32,
after an instruction's distance to its end is taken off: from
`lea rax, [rip + x]`, a source addend of INT32_MIN + 4 fits and
INT32_MIN + 3 doesn't.

In an executable, every code reference to a symbol goes the same way: directly
for the program's, through the slot for an import. So the kernel reads one
32-bit column per symbol, `reference`, and the kind chooses only the
arithmetic and the width. Most fixups are `CALL`, `CALL_GOT` and `PC32`, the
same S + A − P on a 4-byte field. The loop is built for those, and the others
go in a separate, short loop.

Fixups use 0.4 bytes per byte of code at the measured density.

**Memory.** Every table lives in an arena, and outputs are lane-owned (see
"Lanes"). The address space is reserved up front, committed on touch, and
backed by transparent huge pages: they fault 2.8× faster and tear down 10×
faster.

All the tables' columns come from one arena, each with room for counts given
up front (count, place, fill: `link_initialize`, with `link_capacity_bytes`
saying how much to reserve), so a small program's columns share a few base
pages. An arena per column would cost a reservation for each of the 26
columns (1.8 µs each, 47 µs in all, more than hello world's whole link is to
take) and a huge page each once past its first 64 KB. Names' bytes go to an
arena of their own, which grows as names are interned.

An arena's first 64 KB stay in 4 KB pages (`MADV_NOHUGEPAGE`), and huge pages
start right after them, on a 2 MB boundary. A huge page is cleared whole when
it is first touched: 15.1 µs on the 9700X, against 0.75 µs for a 4 KB page
and 4.7 µs for all sixteen of the first 64 KB (`benchmark-lanes`). With
transparent huge pages set to "always", as on the 9700X, even an arena used
for a few kilobytes would otherwise pay for a huge page. Reserving and
releasing a 64 GB arena costs 1.8 µs.

Logical length and capacity are always separate. A full arena is an error
that is reported, never a truncation.

### Fixups from the encoder

The encoder already knows where every byte goes. Its offsets output gives each
instruction's start, and the template gives the immediate's width, so a field's
offset follows without decoding anything again:

- **A displacement field:** offset = instruction end − immediate bytes − 4.
- **An immediate or a branch's rel32** (always the last field): offset =
  instruction end − its width.

RIP-relative displacements are always 32-bit, so EVEX's compressed
displacements never apply.

The addend has to account for the CPU measuring from the instruction's end:
A = (the source's addend) − (instruction end − field offset). That gives:

| Instruction | A |
| --- | ---: |
| `call f` | −4 |
| `lea rax, [rip + x]` | −4 |
| `cmp byte ptr [rip + x], 1` (imm8 after the displacement) | −5 |
| `mov dword ptr [rip + x], imm32` | −8 |

"PC-relative means −4" would be wrong.

The interface is sparse: the code generator marks the instructions with
symbolic operands. The form number's bit 15 is free (the largest form
number, the last form with a GS override, is 17,132), so marked instructions
can carry it. The encoder then returns their end offsets. Two designs to
measure against each other:

- **All offsets:** the existing "with offsets" loop, 0.72 ns per instruction
  against 0.64 ns. The caller picks the marked ones.
- **Marked offsets only:** a `vpcompressd` of each group's offsets by the
  marked lanes, stored to the fixup list. On Zen 5 that is one more FP1/FP2
  µop per group plus a store, where Zen 4's memory compress is microcoded.

Only a branch within the function is filled in before encoding. A local-exec
TLS offset is known as soon as the program's TLS block is laid out, but it
stays a fixup (`TPOFF32`): an object written from the same encoded state has
to leave it to the linker that combines it with other objects' TLS. With the
offset frozen, the object would read another variable's storage.

Three rules for the encoder's side:

- **Branches within a function** are resolved by the code generator before
  encoding, through labels. Their rel8 or rel32 forms come from a relaxation
  loop over the encoder's lengths (Phase II alone, which the encoder document
  lists as the next step). They never become fixups.
- **The encoder's 64-byte stores** run up to 64 bytes past an instruction
  stream's end, within `BUSTER_X86_ENCODER_CAPACITY`. A lane's functions are
  encoded one after another into the lane's own buffer, so each overrun lands
  in space the next function overwrites. The padding after a function (to 16
  bytes, filled with `int3`, as lld does) is written after it is encoded. The
  last function's overrun lands in the buffer's capacity. Nothing is ever
  encoded straight into a range another lane owns.
- **Nothing is decoded later** to find a field.

The cross-check is GNU as. The encoder's tests already assemble random streams
with it; giving some instructions symbolic operands (`call f`, `cmpb $1,
x(%rip)`) and comparing GNU as's relocation records (offset, type, addend) with
the fixups pins all of this down.

`link_add_code_fixups` (`link.c`) makes an atom's fixups from the encoder's
output. The code generator gives a reference per symbolic field: the
instruction, which field (a displacement and an immediate can both be
symbolic), the kind, the target and the source's addend. With the fixup ends
and `x86_fixup_distance`, that gives each fixup's offset and addend.

- **It checks each kind against its field:** a rel32, a RIP-relative
  displacement, another 32-bit displacement or immediate, or a 64-bit
  immediate, from the form and the operands.
- **A displacement after a base needs a placeholder that doesn't fit in 8
  bits,** such as INT32_MIN. Otherwise the encoder makes it 8-bit or leaves it
  out, and there is no 32-bit field to patch.
- **It zeroes the fields in the atom's bytes,** as GNU as writes them. The
  object writer then writes the bytes as they are, and the executable patches
  over them.

`test-link` checks each kind's offset, addend and bytes against what GNU as
makes of the same instruction, alone and in streams across the encoder's
groups.

### The direct path (no `-c`)

Without `-c`, one command compiles and links: `cc main.c -o main`, rather than
`cc -c main.c -o main.o` followed by `cc -o main main.o`. When every input is a
C source compiled in this process (one or many translation units, a unity
build or not), the executable is written straight from the compiler's state,
and no object file is written or read. Foreign `.o` and `.a` inputs fall back
to the import path (see "Linking other compilers' objects"), which produces
the same representation, so a link can mix both.

"Direct" is about the missing object file, not about the C library. The
direct path links the C runtime as `cc` does, through the runtime snapshot
below. Executables without libc, whose `_start` makes system calls (as with
`-nostdlib`), are one more output it writes, and the first one built (see
"Plan").

The steps:

1. compile every translation unit;
2. bind every external name;
3. select instructions, with the binding known;
4. encode each function into its lane's buffer;
5. lay out: the sizes are known now;
6. patch and write, fused, chunk by chunk;
7. write the headers, the synthetic sections and the build ID.

#### Binding before instruction selection

Once the front end has seen every translation unit, each referenced name with
external linkage is classified before any instruction is selected. That is
the decision a linker makes at the end, made at the start, so instruction
selection never has to guess:

| The name is defined in | Code that refers to it |
| --- | --- |
| the program | direct: `call rel32`, `lea`/`mov` RIP-relative |
| the runtime's shared libraries (libc.so.6 and others) | through the GOT: `call [rip + slot]` (`ff 15`, 6 bytes) and `mov reg, [rip + slot]` for data |
| a runtime archive (`-static`, or libgcc) | direct, since its members are placed in the image |
| nowhere, as a weak reference | a GOT slot holding 0 |
| nowhere | an error with the name and the referencing functions |

What follows from this:

- **No PLT.** Executables are bound at load time (`DT_FLAGS` BIND_NOW,
  `DT_FLAGS_1` NOW and PIE, with full RELRO). This is what `-fno-plt` does,
  and Arch links its packages with `-z now` and `-z relro` anyway.
- **No copy relocations.** Data imported from libc (`stdout`, `environ`) is
  reached through its GOT slot.
- **Function pointers compare equal everywhere:** a pointer to `puts` is
  libc's own address, loaded from the slot.
- **Thread-local variables** the program defines use local-exec (`mov rax,
  qword ptr fs:[offset]`), whose `TPOFF32` fixup is patched once the TLS block
  is laid out. Imported ones, rare in C (libc's `errno` goes through
  `__errno_location`), use initial-exec through a GOT slot with a `TPOFF64`
  dynamic relocation.

`-no-pie` is the same image at a fixed base (0x400000), without relative
relocations, and with `ABS32` fields allowed.

Duplicate strong definitions are an error. With `-fno-common` (the default
since GCC 10), tentative definitions are definitions, so duplicates of those
are too. A weak definition loses to a strong one.

**Exports.** A program definition goes into `.dynsym` and `.gnu.hash`, even
without `-rdynamic`, when a shared library in the link:

- defines the same name: the program's own `malloc` then interposes libc's, and
  libc's references bind to it;
- refers to it: a callback that a library calls, which the loader would
  otherwise report as undefined.

GNU ld and lld export both kinds, so the snapshot records each shared
library's undefined names as well as its definitions.

**Classify early, or relax later.** Foreign objects in the same link are
imported first, so their definitions are known before instruction selection.
If selection starts before an input's symbols are known, it uses the GOT form
with an `REX_GOTPCRELX`-style fixup, and a local target turns `mov` into `lea`
at layout. That is the same length, so the layout doesn't move.

#### The runtime snapshot

A dynamic PIE on this system needs:

- `Scrt1.o` (38 bytes of `.text`), `crti.o` and `crtn.o` (`.init`/`.fini`
  prologues and epilogues), `crtbeginS.o` (233 bytes) and `crtendS.o`;
- `libc_nonshared.a` (four members: `atexit`, `at_quick_exit`,
  `pthread_atfork`, `__stack_chk_fail_local`);
- libgcc and `libgcc_s.so.1` when referenced;
- `libc.so.6` itself: 3,189 dynamic symbols, and version definitions from
  `GLIBC_2.2.5` to `GLIBC_2.44`, `GLIBC_ABI_DT_RELR` among them. It is found
  through `/usr/lib/libc.so`, a linker script naming it,
  `libc_nonshared.a` and `ld-linux-x86-64.so.2`.

mold and wild open, map and parse all of this on every link. The compiler does
it once and keeps the result in a snapshot under `$XDG_CACHE_HOME/buster/`.

**Contents:**

- **The crt objects and `libc_nonshared.a`,** imported and laid out together.
  Their internal references are resolved. What remains are fixups against the
  program and the dynamic sections: `main`, `__libc_start_main@GLIBC_2.34`
  through the GOT, `_init`/`_fini`, `__dso_handle`, the `.init_array` entries
  (`frame_dummy`), and weak undefined references (`__gmon_start__`, the
  `_ITM_*` clone table functions) that resolve to 0.
- **libc.so.6's exports:** an open-addressing table keyed by the front end's
  name hash, holding each name's default version, type and size, and the
  library's undefined names, which decide the program's exports. It is a few
  tens of kilobytes. The other shared libraries in the link get the same.
- **For `-static`, libc.a's 2,227 members:** each member's atoms and fixups,
  already converted; a table from symbol to defining member; and each member's
  undefined names. libgcc.a and libgcc_eh.a get the same.
- **The search results** that led to each file: the paths probed in the `-L`
  order and which ones were absent.

**Validation.** The key is the compiler's version, the options that change the
runtime (`-static`, `-pie`, `-nostdlib`, the search paths) and the identity of
each file: `st_dev`, `st_ino`, `st_size`, `st_mtim` and `st_ctim`. A package
upgrade replaces the inode and changes all of these. `st_ctim` changes on any
write, and no system call sets it.

Checking costs one `stat` per file and per absent candidate: about ten calls
at 0.18 µs. The absent candidates are checked so that a library newly placed
earlier in the search path is noticed. Reading the snapshot is one `pread` of
a few kilobytes (or more for `-static`).

A stale or missing snapshot is rebuilt by the import path (the same code as a
full link), written to a temporary file and renamed into place. Two compilers
rebuilding it at once write identical files, so no lock is needed.

**Static links.** The closure over libc.a is GNU ld's group semantics, which
the driver asks for (`--start-group -lgcc -lgcc_eh -lc --end-group`):

1. Start from the program's undefined names that aren't weak.
2. For each one, find the defining member in the snapshot's table.
3. Load that member, and add its own undefined names that aren't weak and that
   nothing defines yet.
4. Repeat until nothing is added.

Weak references load no member, as with GNU ld. They bind to a definition that
was loaded for another reason, or to 0: a program that tests `puts` declared
weak must see it null under `-static` unless something else pulls `puts` in.
A member defining a name the program also defines is only pulled in by another
of its names, and then the duplicate is an error, as with GNU ld. Members are
laid out in their archive order, whatever order the walk visited them in.

**What the snapshot buys.** Synthetic sections and patching for the runtime
are a few dozen records. `__libc_start_main`, `__cxa_finalize` and the
program's imports each get a GOT slot and a `GLOB_DAT`. `__dso_handle` and the
two init/fini entries become relative relocations. No ELF is parsed on the
link's path.

#### Layout

Segments in lld's order, which loaders and tools expect:

1. **R:** the ELF header, program headers, `.interp`, the notes, `.gnu.hash`,
   `.dynsym`, `.dynstr`, `.gnu.version`, `.gnu.version_r`, `.rela.dyn` and
   `.relr.dyn`, `.rodata`, `.eh_frame_hdr` and `.eh_frame`.
2. **RX:** `.init`, `.text` and `.fini`.
3. **RW, RELRO:** `.tdata` and `.tbss`, `.init_array`, `.fini_array`,
   `.data.rel.ro`, `.dynamic` and `.got`. The segment's memory size runs to
   the end of its page, as lld's `.relro_padding` does, so that
   `PT_GNU_RELRO` protects all of it.
4. **RW:** `.data` and `.bss`.

These are four `PT_LOAD`s, as lld writes for hello world, with `PT_PHDR`,
`PT_INTERP`, `PT_DYNAMIC`, `PT_GNU_RELRO`, `PT_GNU_EH_FRAME`, `PT_GNU_STACK`
(not executable), `PT_NOTE` and `PT_TLS` when there is TLS.

File offsets are contiguous. Each segment's address continues at the next page
plus its offset within a page, so there is no file padding, and segments stay
4 KB-congruent.

The TLS segment then needn't start on a multiple of its alignment: `.tdata`
starts at its own alignment, which can be less than `.tbss`'s. The TLS block
keeps `p_vaddr`'s remainder f modulo `p_align`, so it starts
align_up(`p_memsz` + f, `p_align`) − f below the thread pointer, as glibc
allocates it and GNU ld and lld compute `TPOFF`: a variable's `TPOFF` is its
offset in the segment less that distance. With f = 0 it is just `p_memsz`
rounded up, which is all an aligned segment exercises.

Exact layout is a recurrence (x′ = align_up(x, a) + s), not a prefix sum of
`size + align − 1`. It becomes a plain prefix sum in two ways:

- **Code:** every function starts at a multiple of 16, and its size is padded
  to 16. Then any concatenation of function sequences (one per lane) gives the
  same addresses as one lane doing them all, so the output doesn't depend on
  the lane count.
- **Data:** within a class, objects are ordered by alignment, largest first.
  In C a type's size is a multiple of its alignment, so the running offset
  stays aligned for every object after it, and the layout is a plain prefix
  sum with no padding at all.
  - An object declared with more alignment than its type
    (`_Alignas(64) char x[10]`) is padded to a multiple of it.
  - C promises nothing about the relative placement of distinct objects.
    `-fno-toplevel-reorder` keeps source order and uses the recurrence.

  Sections whose order is semantic (`.init_array` priorities, `.init`
  fragments, crt objects) keep their order, and their few atoms use the
  recurrence. An over-aligned function, past 16, goes first in `.text`.

Everything except `.text`, `.eh_frame`, `.eh_frame_hdr` and the symbol table
is sized before any function is encoded:

- data sizes come from the front end;
- dynamic sections from the binding;
- the GOT from the number of imports;
- relative relocations from the number of pointers in initializers.

#### Patching and writing

After layout, the lanes take the output's chunks, in parts by bytes from a
counter (rule 1 in "Lanes"). Chunks are about 256 KB, a quarter of Zen 5's L2.

**Chunk boundaries never split a field.** They are computed from the image and
its fixups alone. Boundary k is at k × 256 KB of the file, unless an atom
holds it:

- an atom of at most a chunk ends the chunk at its own end, since no field
  crosses an atom's end;
- in a larger atom the boundary stays where it is, unless a field crosses it,
  and then it moves to the field's end.

A 4-byte field two bytes before a 256 KB boundary is ordinary (GNU as puts a
`call`'s at `0x3fffe` after 262,141 bytes of padding). A lane patching such a
field could otherwise change bytes the next lane had already hashed or
written. Because the boundaries don't depend on the lane count, the build ID's
chunks don't either. `test-executable` puts 4- and 8-byte fields at every
place across the nominal boundaries.

For each chunk the lane:

1. applies the chunk's fixups (sorted by offset already);
2. hashes it for the build ID;
3. writes it with `pwritev`, from where its bytes are: the atoms' own buffers,
   the linker's headers and GOT, and fill for the gaps between them (`int3`
   in `.text`, zeros elsewhere).

The chunk is pulled into L2 by the first step and written from L2, where
`pwrite` into an existing page reaches 41 GB/s on tmpfs. Each byte of the
output moves once from its buffer, and the fixup columns are read in order.
Measured on 440 KB, 4 MB and 46 MB of bytes, on tmpfs and btrfs, writing each
chunk as it is hashed costs what one `pwrite` of the whole file does, to within
−17% and +16%. Hashing every chunk first and then writing the file once cost up
to 22% more, the chunks having left L2, and copying each chunk into a staging
buffer before writing it 3–65% more.

**The patch kernel** reads, per fixup:

- the field offset;
- the target's 32-bit `reference`, its image offset, from a table that stays
  in L2 up to about 250,000 symbols;
- the addend.

Then it computes S + A − P and stores 4 bytes. That is `CALL` and `PC32`,
which most fixups are, and `executable_patch_pc32` takes every fixup of an
atom that way. A vector compare of the kind column then finds the others, 64
fixups at a time, and `executable_patch_other` redoes each of them over what
the loop wrote: any kind, with addends from the side table. Neither checks
S + A − P per fixup. The count phase finds the largest addend magnitude with a
vector maximum, and while the base, the image and that addend stay below 2^31
together, no `CALL` or `PC32` value can leave int32. Otherwise every fixup
goes through `executable_patch_other`, which checks each value's range. A
value that doesn't fit fails the link, naming the fixup, and its field is
zeroed, so that both paths leave the same bytes. `benchmark-executable`, ns
per fixup on CPU 2, on the synthetic cases of "SIMD" (every fixup `CALL` or
`PC32`):

| Kernel | 64 KB, 2,000 fixups, 10,000 targets | 1 MB, 30,000, 100,000 | 64 MB, 2 M, 1 M |
| --- | ---: | ---: | ---: |
| `executable_patch_pc32` | **0.37** | **0.53** | **1.40** |
| a branch on the kind per fixup, the other kinds out of line | 0.73 | 0.74 | 1.42 |
| `executable_patch_other` for every fixup (the checked path) | 1.47 | 1.64 | 2.52 |

Clang unrolls `executable_patch_pc32`'s loop eight times and leaves the
branching one rolled; an earlier build of the same source measured the
branching one at 0.48 ns in L1. On the generated programs, half of whose fixups are of the other kinds so
that the tests cover every kind, the kernel takes 1.5–1.7 ns per fixup, and
3.1 ns on the smallest program's 91: 2.16 million fixups take 3.7 ms. A
compiled program's fixups are mostly `CALL` and `PC32`, since the direct path
reaches the program's own symbols directly.

**The build ID** is a tree of the chunks' hashes: each chunk is hashed while it
is in L2, and the build ID is the hash of the chunks' hashes in chunk order.
The lanes compute it without depending on each other, and the result doesn't
depend on the lane count. The note's 16 bytes are hashed as zeros and written
last, after every chunk.

GNU ld's default style, SHA-1, and the others it offers (md5, uuid, an
explicit value, none) are to be honored when asked for. The direct path's own
default is a 128-bit hash of AES rounds, decided by measurement. Eight ZMM
accumulators take the stream's 64-byte blocks in turn, each block as the round
key of one `vaesenc` of its accumulator, followed by a round with a constant
key. Then the accumulators fold into one, the length goes in, and the four
128-bit lanes fold into the 16 bytes. It isn't cryptographic: a build ID only
has to tell builds apart. The candidates, GB/s on CPU 2, medians of 21 passes:

| Hash | 4 KB (L1) | 256 KB (L2) | 1 MB | 64 MB |
| --- | ---: | ---: | ---: | ---: |
| 8 accumulators, one `vaesenc` per block | 430 | 259 | 308 | 87.5 |
| 4 accumulators, one `vaesenc` per block | 391 | 222 | 315 | 86.9 |
| 8 accumulators, two rounds per block (`executable.c`) | 300 | **257** | 294 | 82.7 |
| multiply-accumulate, as XXH3, 4 accumulators | 144 | 168 | 157 | 68.4 |
| reading alone (an XOR of the blocks) | 699 | 258 | 315 | 87.8 |

In L2, where the chunk is, the chosen hash runs as fast as reading it, and its
second round mixes each block into its accumulator before the next one comes.
With each of the four, flipping any input bit of 64 or 600 bytes flips 64 of
the 128 output bits on average. Clang unrolled the hash's loop four times,
which took it down to 176 GB/s in L2, so the loop asks it not to. In
`benchmark-executable` it hashes 256 KB in L2 at 260 GB/s (1.0 µs), in pieces
of 4 KB at 250, in pieces of 100 bytes at 9.0 (a chunk of many small data
atoms: 11 ns a piece), and 64 MB at 82.

**Writing the file:** what the output is comes from `lstat`, which opens
nothing and needs no permission on the file. Opening a FIFO to look at it
would count as a reader or a writer: it would release a reader waiting for a
writer, which could then read an end of file and leave. `fstat` then checks
what was opened, in case the path changed in between, and a file that no
longer qualifies for 1 or 3 is written beside the output, as in 2. The
`lstat` costs 0.2–0.3 µs of a link in place (its open went from 0.5 to
0.7 µs). An `O_WRONLY | O_NONBLOCK` open would classify without it, since a
FIFO without a reader fails it with ENXIO untouched, but in place would then
be `O_WRONLY`, and a shared mapping (5) needs `O_RDWR`.

1. If the output exists, is a regular file with one link, is this user's and
   isn't running, open it with `O_RDWR` and overwrite it in place. Then
   `ftruncate` it to the new size and set its mode (0777 less the umask, as a
   new file gets), each only if it changed. In place is 1.2–3.6× faster than a
   new file (table above). mold and wild do the same for executables.
2. If opening it fails with ETXTBSY (the program is running), or the file has
   other hard links, or is someone else's (whose mode `fchmod` couldn't set),
   or it is a symbolic link (replaced, not written through: the open in place
   has `O_NOFOLLOW` too), or the output is a shared library (which
   running processes map without ETXTBSY to protect them): write a new file
   with `O_CREAT | O_EXCL` beside it, named after it with `.tmp`, the process
   id and 16 random hexadecimal digits (so that a file a killed link left
   doesn't block the next), and rename it over the old one. If the output
   doesn't exist, create it.
3. If it is a file of another type, such as `/dev/null` or a FIFO, open it
   once, with `O_WRONLY` (a FIFO's open waits for a reader), and write into
   it, in order, as GNU ld and lld do, rather than replace it: every chunk is
   patched and hashed first, and lane 0 writes the whole file at the end with
   the build ID in place. Such a file is never replaced or removed.
4. Never `O_TRUNC`: on btrfs it costs 58 µs for 16 KB and 10.4 ms for 4 MB.
5. **Each lane writes the chunks it patches, with `pwritev`.** One `pwrite`
   reaches 6.8–12.6 GB/s on tmpfs for a new file and 11.7–41.6 GB/s in place;
   on btrfs, 4.4–7.5 and 6.4–9.4. The lanes' writes take turns at the file's
   lock in the kernel, so their speeds don't add up, but each write overlaps
   the other lanes' patching and reads its chunk from its own L2 (measured
   below). **Several lanes copying into a shared mapping** of the file scales
   on tmpfs (14–19 GB/s with eight) and not on btrfs, so step 8 may choose by
   `statfs`'s filesystem type, from measurements per filesystem, including
   whether `fallocate` and `MADV_HUGEPAGE` help (they did on mold's ext4 and
   didn't here).
6. Close the file before returning: `execve` of a file open for writing fails
   with ETXTBSY. Don't `fsync`, like every other linker.
7. A link that fails after opening the output removes what it wrote rather
   than leave a half-written executable: the output written in place or
   created, or the file beside it, which leaves the old output and its other
   links as they were. One that fails before opening leaves everything
   alone.

**One writer, or every lane.** This section first had one lane write
everything, which keeps the file's writes in one place, but that lane becomes
the bottleneck. Three ways, measured with the 180,000-worker program of "The
first executables" (35.5 MB, 136 chunks) on the 9700X, µs, in place / as a new
file on tmpfs:

| Lanes | Lane 0 writes after the patching | Lane 0 writes 4 MB waves while the others patch the next | Every lane writes its chunks |
| ---: | ---: | ---: | ---: |
| 2 | 7,070 / 9,090 | 5,930 / 6,610 | **4,940 / 5,590** |
| 4 | 6,130 / 8,160 | 5,070 / 6,540 | **4,290 / 5,780** |
| 8 | 5,820 / 7,480 | 5,070 / 6,410 | **4,000 / 5,550** |

The first column's figures are sums of phases, from a harness that started the
lanes for every link. With waves on 8 lanes, lane 0 spent 4.2 ms writing,
where one `pwrite` of the same bytes from memory takes 2.3 ms, while the other
lanes patched their 19 chunks each at a quarter of one lane's speed per chunk
and then waited for it: the patching and the copy compete for DRAM, and lane 0
reads what other cores wrote. On btrfs, at 8 lanes over three runs, every lane
writing took 6.7–7.4 ms in place against 8.4–8.8 with waves, and 7.1–8.5 ms
for a new file against 7.4–7.7, the one case where a single writer can win.

#### The first executables: static, without libc

Step 4's executables are static and use no libc: no crt objects, no dynamic
sections, and a `_start` that makes its own system calls, as with `-nostdlib
-static`. `executable_write` (`executable.c`) lays them out, patches them and
writes them from the representation, at GNU ld's fixed base, 0x400000:

- **Sections and segments:** the layout above without the dynamic sections.
  R holds the ELF header and the program headers (where a program finds its
  `PT_TLS` through `AT_PHDR`), `.note.gnu.build-id` and `.rodata`, string
  literals after the other constants; RX `.text`; RELRO `.tdata` and
  `.tbss`, `.init_array`, `.fini_array`, `.data.rel.ro` and `.got`; RW
  `.data` and `.bss`. `.shstrtab` and the section headers follow, outside
  the segments. The program headers are lld's, in its order: `PT_PHDR`, the
  `PT_LOAD`s, `PT_TLS`, `PT_GNU_RELRO`, `PT_GNU_STACK` and `PT_NOTE`.
  `.tbss` takes no address space, and a section without bits has the file
  offset where its bytes would start, as lld gives it.
- **Atoms:** counted per bucket, a class and an alignment, and placed by
  prefix sums of their padded sizes, largest alignment first. Functions are at
  least 16-byte aligned and padded to 16 with `int3`. The init and fini arrays
  keep id order, as whole 8-byte entries. Atoms aligned past a page are
  refused for now.
- **The GOT:** a slot for each target of `CALL_GOT`, `LOAD_GOT`, `GOTPCREL`
  and `GOTTPOFF`, numbered from 1 in symbol order: the target's address, its
  offset from the thread pointer for TLS, or 0 for a weak symbol defined
  nowhere. Nothing is relaxed, and a static executable needs no dynamic
  relocations. The generated programs use the GOT kinds to test them, where a
  compiled program's references to its own symbols are direct.
- **Symbols:** `reference` is the image offset (the address less the base); a
  TLS symbol's is its offset from the thread pointer (variant II: from the end
  of the TLS block). The writer starts the TLS segment on a multiple of its
  alignment, `.tdata` taking `.tbss`'s when that is larger, so f is 0 in
  "Layout"'s formula and the block's end is the segment's memory size rounded
  up to its alignment. A weak symbol defined nowhere gets address 0's. The linker defines
  `__init_array_start`, `__init_array_end`, `__fini_array_start` and
  `__fini_array_end`. Any other symbol defined nowhere fails the link, naming
  it, even when nothing refers to it, and so does an import from a shared
  library until step 5. A weak thread-local symbol defined nowhere gets an
  offset of 0 from the thread pointer, in its `TPOFF32` fields and its GOT
  slot alike, as GNU ld gives it.
- **Limits:** the image must end below 2 GB and the TLS block be at most
  2 GB, so that every image offset, PC-relative value and offset from the
  thread pointer fits in 32 bits (`.tbss` takes no address space, so the
  image's limit doesn't bound it). Atoms are at most 2 GB each, checked
  before their padding is added, so that no sum of sizes wraps.
- **Errors** don't depend on the lane count: the entry point is checked
  first, each phase reports the first item in id order that fails, and a
  failing fixup is the smallest that does. Each phase's errors go in a row of
  their own, so a lane that has gone on to the next phase can't change what
  the others still read of the last one.

Its phases, each lane taking a range of the atoms, fixups, symbols or GOT
words (`lane_range`, since each loop is a few nanoseconds per item), or parts
of the chunks:

1. **count:** the atoms per bucket; the GOT kinds' targets, into a bitset per
   lane, from a vector compare of the kind column; and the largest addend.
   The lanes' bitsets are then ORed, a range of words each;
2. **layout:** every lane lays out the sections from the counts, then places
   its own atoms after those of the lanes before it, and numbers its words'
   GOT slots;
3. **symbols:** each lane fills its symbols' references and GOT slots, and its
   chunk boundaries, and lane 0 writes the headers;
4. **open:** lane 0 opens the output for every lane;
5. **chunks:** patch, hash and write, as above;
6. **finish:** lane 0 hashes the chunks' hashes, writes the build ID and
   closes the file.

`benchmark-executable` writes generated programs of about hello world's, the
test defaults', tcc's and synth's sizes. Medians in µs on the 9700X, the output
on tmpfs, in place / as a new file, with one lane per core, against the floor
(one `pwrite` of the same bytes from memory, opened and closed):

| Program (file) | 1 lane | 2 lanes | 4 lanes | 8 lanes | Floor |
| --- | ---: | ---: | ---: | ---: | ---: |
| 1 worker (3.9 KB) | **2.7 / 3.4** | 3.2 / 3.8 | 4.1 / 4.6 | 5.9 / 5.8 | 0.6 / 1.3 |
| 64 workers (16 KB) | **5.0 / 6.3** | 5.9 / 7.2 | 6.3 / 8.0 | 7.0 / 8.6 | 0.8 / 2.2 |
| 2,000 workers (397 KB) | 70 / 87 | 48 / 68 | 45 / 66 | **43 / 63** | 8.7 / 24 |
| 180,000 workers (35.5 MB) | 8,520 / 9,850 | 4,990 / 5,550 | 4,210 / 5,560 | **4,110 / 5,680** | 2,570 / 4,240 |

The largest has 184,531 atoms and 2.16 million fixups in 136 chunks. Its
phases in place, µs:

| Lanes | count | layout | symbols | open | chunks | finish |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | 828 | 505 | 571 | 8.0 | 6,615 | 2.2 |
| 8 | 334 | 108 | 68 | 7.9 | 3,603 | 2.5 |

On btrfs (`/home`), the 397 KB program takes 105 / 107 µs on one lane and
71 / 92 on eight (floor 43 / 42), and the 35.5 MB one 11.4 / 11.3 ms and
6.4 / 7.1 (floor 5.5 / 5.7).

- **Small programs pay fixed costs.** The 3.9 KB one spends 0.8 µs in the
  three phases before the file, 0.7 µs opening it in place (`lstat`, `open`
  and `fstat`; 1.3 µs creating it), 0.9 µs on its one chunk and 0.3 µs
  closing it, where one `pwrite` of the file costs 0.6. For it and the 16 KB one, more lanes cost more than they save:
  their syncs and parts outweigh the work. From 397 KB they pay.
- **Large programs pay for memory.** On one lane, 35.5 MB takes 3.3 times the
  floor in place: 3.7 ms of patching, the write, and 1.9 ms of count, layout
  and symbols, loops over atoms, fixups and symbols whose columns come from
  DRAM. On eight lanes it takes 1.6 times the floor, and the chunks are
  DRAM-bound: the patching reads 35 MB of code and 28 MB of fixup columns and
  writes the code back, and the page cache takes its copy.
- **The count phase reads every fixup** for the GOT's targets and the largest
  addend. Step 5's binding knows the imports before any code is selected, so
  the GOT can be sized before the code exists, as "Layout" says, and the
  addends' bound can come from the code generator.

#### Synthetic sections

| Section | Contents |
| --- | --- |
| `.interp` | `/lib64/ld-linux-x86-64.so.2` |
| `.dynamic` | `DT_NEEDED` for each runtime library actually referenced (as `--as-needed`), `DT_FLAGS`, `DT_FLAGS_1`, `DT_DEBUG`, `DT_INIT`/`DT_FINI` and the array entries, the table addresses and sizes, `DT_RELR`/`DT_RELRSZ`/`DT_RELRENT` or `DT_RELA`/`DT_RELACOUNT`, `DT_VERNEED`/`DT_VERSYM` |
| `.dynsym`, `.dynstr` | imports, and the program's definitions that a shared library in the link also defines or refers to (see "Exports") |
| `.gnu.hash` | those exported definitions, so the loader can find them (empty when there are none); every global definition with `-rdynamic` and in shared libraries |
| `.gnu.version`, `.gnu.version_r` | each import's default version from the snapshot, and the needed version names per library |
| `.got` | one slot per imported or weak-undefined symbol |
| `.rela.dyn` | a `GLOB_DAT` per import, an `R_X86_64_64` per pointer to one in data, and an `R_X86_64_RELATIVE` per pointer at an odd address |
| `.relr.dyn` | the other relative relocations, packed as RELR when libc defines `GLIBC_ABI_DT_RELR` (it does here, so the executable also needs that version) |
| `.eh_frame`, `.eh_frame_hdr` | a CIE and an FDE per function from the code generator, merged with the runtime's |
| `.symtab`, `.strtab`, `.shstrtab` and section headers | for debuggers and profilers, after the loaded segments |

`.eh_frame_hdr`'s table is already sorted, because functions are laid out in
order. `.strtab` is the needed names copied from the name arena.

There is no `.note.gnu.property`. The output's property is the AND of its
inputs', and the program's code carries no IBT or shadow stack markers.

RELR is a list of addresses and 63-bit bitmaps of the 8-byte words after them.
It is built from the sorted offsets of the `ABS64` fixups in one pass. Where
RELA takes 24 bytes per relocation, RELR takes 8 for an isolated pointer and
under a bit per pointer in a dense table. Arch links its own packages this way
(`-z pack-relative-relocs` in `makepkg.conf`).

**RELR can only hold even addresses.** An entry's low bit tells an address
from a bitmap. A pointer at an odd address, where a packed structure can put
it, goes to `.rela.dyn` as `R_X86_64_RELATIVE`. GNU ld and lld do the same:

- they pack pointers at even addresses, those 2 or 4 bytes past a multiple of
  8 among them;
- they keep the odd ones in `.rela.dyn`.

The initializers say how many pointers fall on odd addresses, so both tables
are sized before layout.

#### Streaming the output

At the end of compilation, the direct path above still writes the whole file.
For large programs that is the page cache floor (3.3–8.8 ms for 46 MB). Writing
during compilation removes it from the end, for every byte that doesn't wait
on an address:

- **Everything the code refers to, placed before it.** lld's order puts the
  GOT, `.data` and `.bss` after `.text`, so their addresses wait for the code's
  size, and so does every reference to them. Streaming uses another order:
  1. R: headers, dynamic tables, `.rodata`;
  2. the RELRO segment with the GOT;
  3. `.data` and `.bss`;
  4. RX: the code;
  5. `.eh_frame` and `.eh_frame_hdr` in an R segment of their own.

  Every address the code refers to is then fixed before the first function is
  encoded. glibc 2.44's loader runs a PIE laid out this way on both kernels
  here (6.18 and 7.2; for the test, lld linked it with a linker script). Data
  without pointers can be written as soon as it is produced.
- **Debug information:** with DWARF 5 nearly all of it is
  position-independent. Addresses go through `.debug_addr`, and the line
  table's `DW_LNE_set_address` once per sequence. So `.debug_info`,
  `.debug_str` and most of `.debug_line` can be written as they are produced,
  and only `.debug_addr` and the sequence addresses wait for layout. Debug
  information is most of a `-g` output (synth's `-g` build is 126 MB against
  46 MB).
- **Code, with one lane:** in that order, every reference to data or a GOT
  slot, and every call to a function already encoded, can be filled in right
  away. Without it, data references wait for the end too. Only calls forward,
  to functions not yet encoded, wait. Compiling callees before callers (a
  reverse topological order of the call graph, ties broken by source order)
  leaves forward calls only on cycles. Each chunk of code can then go to the
  file as soon as it is full, and the few calls waiting are patched at the end
  through a mapping of the file.

Streaming is the step after the first implementation, measured against it,
along with checking the data-first order with debuggers and profilers. With
several lanes, a lane's base address isn't known until every lane before it
has finished, so code that crosses lanes waits for the end anyway.

Fixing forward calls by sending them through a table (one indirect call each)
would make code streamable in any order, but at a permanent cost to the
generated program. It belongs to an incremental development mode, if
anywhere.

### The object writer (`-c`)

The object writer serializes the same state as an ET_REL file that GNU ld,
lld, mold, wild and this linker accept. Its structure is count, place, fill:

1. **Count:** sections, local and global symbols, relocations per section,
   string-table bytes. Most of it is counted while the code is produced.
2. **Place:** section indices, symbol indices, string offsets and file ranges,
   with checked arithmetic.
3. **Fill:** each range is written once, into one buffer per lane, then one
   `pwritev`. Nothing is allocated per symbol or relocation, and no output
   buffer grows.

**Sections:**

- `.text`, or `.text.<name>` per function with `-ffunction-sections`;
- `.rodata`, and `.rodata.str1.1` for string literals, marked `SHF_MERGE |
  SHF_STRINGS`;
- `.data`, `.data.rel.ro`, `.bss`, `.tdata`, `.tbss`;
- `.init_array` and `.fini_array`, with priorities in their names;
- `.eh_frame` (`SHT_X86_64_UNWIND`);
- `.note.GNU-stack`, which must be there or linkers assume an executable
  stack;
- `.comment`;
- `.symtab`, `.strtab`, `.shstrtab` and a `.rela.<section>` for each section
  with relocations;
- `.debug_*` when asked.

**Symbols:**

- the null entry first;
- an `STT_FILE` with the source's name;
- section symbols for the sections relocations refer to, and every local
  definition;
- then the global and weak symbols. `.symtab`'s `sh_info` is the first
  non-local index, and its `sh_link` is `.strtab`.

**Section indices:** past `SHN_LORESERVE` (65,280 sections, reachable with
function sections), indices go in `SHT_SYMTAB_SHNDX` and `e_shstrndx` in
section 0. Internal ids never inherit `st_shndx`'s 16 bits.

**Relocations.** Each fixup's kind names its relocation type (the table in
"Representation"), so the type always matches the instruction. GNU as writes
`R_X86_64_GOTPCRELX` for `call *puts@GOTPCREL(%rip)`; `PLT32` there would make
the call read PLT code as a function pointer. `GOTPCRELX` and `REX_GOTPCRELX`
go only on the instructions the psABI lets a linker rewrite (`call`, `jmp`,
`mov`, `test` and the binary operations), and `R_X86_64_GOTPCREL` on any other
instruction that reads a slot.

For `-c` on its own, the code generator can't know where external symbols end
up, so it chooses forms that let the linker decide:

| Reference | Form | Relocation |
| --- | --- | --- |
| a call | `call rel32` | `PLT32` |
| external data | `mov` through the GOT | `REX_GOTPCRELX`, which a linker turns into `lea` when the target is local |
| the unit's own data | RIP-relative | `PC32` |
| the unit's own TLS | local-exec | `TPOFF32` |
| other TLS | initial-exec | `GOTTPOFF` |
| a pointer in data | | `R_X86_64_64` |

`-fPIC` sends data references through the GOT, and TLS through the
general-dynamic and local-dynamic sequences. The addends are the fixups' (A
relative to the field, "Fixups from the encoder"), and the fields themselves
are zero, as GNU as writes them.

The scalar kernels decide the per-record costs. Writing `Elf64_Rela` from
columns takes 0.18 ns per record in L1 and 0.61 ns from DRAM, and the AVX-512
transpose of three qword columns into 24-byte records (three `vpermt2q` and
three masked permutes per 8 records) takes 0.17 and 0.62 ns. `Elf64_Sym` has
the same shape. Both are store-bound, so they stay scalar.

What an object costs is then its file. For one of synth's translation units
(68 KB of code, 2,000 relocations, 1,400 symbols, 175 KB written), the records
and copies take a few microseconds, against 4–40 µs to write the file (in
place on tmpfs, new on btrfs).

`-c` and the direct path can run from the same encoded state. The object gets
the fixups as relocations, and each chunk before it is patched; the executable
gets the chunk after.

Such an object carries the executable's binding decisions: direct calls into
the program, GOT calls into libc, local-exec TLS. So, like a `-fPIE` object,
it links into executables and not into shared libraries.

### Linking other compilers' objects

Foreign inputs are ELF relocatable objects, archives (with their symbol index,
including thin archives), shared libraries and the linker-script subset that
`libc.so` uses (`GROUP`, `INPUT`, `AS_NEEDED`, `OUTPUT_FORMAT`). The importer
turns them into the same atoms, symbols and fixups, borrowing section contents
from the mapped files. Each step is a lane phase (see "Lanes"):

1. **Open and map** the inputs, split by bytes across lanes. Archive members
   are enumerated from the archive's index, and each is parsed only when step
   4 loads it.
2. **Parse** each object's section headers, symbols and relocation headers into
   lane-local arrays. Then hash every global symbol's name:
   - find the NUL with `vpcmpeqb` over masked 64-byte loads;
   - mix the bytes, for which `vpmullq` (2 cycles latency, 0.5 throughput on
     Zen 5) and `vaesenc` (4 and 0.5) are candidates;
   - most C names fit in one load, and C++'s longer ones loop.
3. **Resolve** through the exchange in "Lanes": each lane owns a fixed range
   of hash partitions and builds each partition's table from records it
   receives in input order. Precedence follows the ELF specification and GNU
   ld:
   1. a strong definition;
   2. a common symbol, which beats a weak definition (ELF §5.2);
   3. a weak definition;
   4. a shared library's definition;
   5. undefined.

   mold ranks weak definitions above common ones (Table 2 of its paper); this
   linker doesn't. Ties go to the earlier input. A hash match is checked
   against the bytes.
4. **Load archive members** whose symbols are needed. This is a frontier walk
   in rounds (new members, their undefined names, the next round) rather than
   GNU ld's left-to-right scan. The results are GNU ld's except where its
   result depends on the order of archives on the command line (a name defined
   in two archives, or an archive needing one that came before it). There it
   behaves as lld and mold do.
5. **Pick COMDAT groups** by signature, through the same exchange.
6. **Garbage-collect** with `--gc-sections`: a frontier walk over the fixup
   graph, with per-lane bitsets OR-reduced between rounds.
7. **Scan relocations:**
   - decode `Elf64_Rela` records into columns, the inverse of the writer's
     transpose: here it is vector work, because classification follows;
   - map each type to its semantic class with a `vpermb` table;
   - mark which symbols need a GOT slot, a PLT entry (a foreign `call`
     through `PLT32` to a shared library needs one, since 5 bytes can't become
     6) or a dynamic relocation, in per-lane bitsets.

   Relaxations (`GOTPCRELX` to `lea`, TLS general-dynamic to initial-exec or
   local-exec) are decided here. They become different fixup kinds and byte
   rewrites with checked patterns. A TLS sequence is rewritten as a pair: the
   `TLSGD` or `TLSLD` relocation and the call to `__tls_get_addr` after it.
8. **Merge strings** (`SHF_MERGE | SHF_STRINGS`, above all `.debug_str`, which
   dominates debug links):
   - split into strings by `sh_entsize`, the character's width:
     - in one-byte sections (`.rodata.str1.1`) a zero byte ends a string, and
       the lexer's bitmask-to-positions method finds them (`vpcompressb` to
       memory costs 3 cycles on Zen 5, against 168 on Zen 4);
     - in two- and four-byte sections (a C `L"..."` string makes
       `.rodata.str4.4`) a zero code unit does, found with `vpcmpeqw` or
       `vpcmpeqd`, and the pieces keep the section's alignment;
     - other widths are copied without merging;
   - hash, exchange through partitions, and deduplicate per partition;
   - assign offsets in partition order, then first-occurrence order;
   - remap each reference into a merged section (a symbol or a section plus an
     offset, possibly into the middle of a string) through the split's
     positions, with a binary search.
9. **Lay out, write the synthetic sections, patch and write,** as in the
   direct path. `.eh_frame` is parsed into CIEs and FDEs: CIEs are
   deduplicated, FDEs of discarded sections dropped, and `.eh_frame_hdr`'s
   table sorted by a radix sort when it is large.

Steps 1–3 and 7 are where most of the foreign path's work is, and where the
vector kernels go. Steps 4–6 are graph walks bound by memory latency.

### Lanes

Fleury's model, adapted to this repository's style: every phase is one
function that every lane runs, with every lane taking its own part of each
loop. The RAD Debugger's base layer has the same (`lane_idx()`,
`lane_count()`, `lane_sync()`, `lane_sync_u64()`, `lane_range()` and
`lane_from_task_idx()` in `base_thread_context.h`), and its batch tool, radbin,
shows how it is used today: it starts its lanes once and runs the whole program
on them. Lane 0 allocates what the lanes share and broadcasts the pointer, and
each lane writes its own part of the outputs. `src/buster/compiler/lane.c` has,
in this repository's style:

```c
typedef struct Range64 Range64;
struct Range64
{
    uint64_t begin;
    uint64_t end;
};

// The calling thread's lane: its index, and the group's size.
static uint32_t lane_index(void);
static uint32_t lane_count(void);
// Waits for every lane of the group. With one lane, returns at once.
static void lane_sync(void);
// Every lane gets source_lane's value. Like lane_sync, it waits for every lane.
static uint64_t lane_broadcast(uint64_t value, uint32_t source_lane);
// The lane that runs narrow task task_index of several spread over the lanes, each of which then broadcasts its result.
static uint32_t lane_from_task_index(uint64_t task_index);
// This lane's part of [0, count): contiguous, in lane order, the first count % lane_count() parts one longer.
static Range64 lane_range(uint64_t count);
// The same, split by cost: cost_prefix holds the inclusive prefix sums of the items' costs, and each item goes to the
// lane whose share of the total cost holds the item's midpoint, so each part's cost is within the largest item's of an
// equal share.
static Range64 lane_range_by_cost(const uint64_t* cost_prefix, uint64_t count);
// Part `part` of [0, count) cut into `parts`, as lane_range and lane_range_by_cost cut it into lanes.
static Range64 range_part(uint64_t count, uint64_t part, uint64_t parts);
static Range64 range_part_by_cost(const uint64_t* cost_prefix, uint64_t count, uint64_t part, uint64_t parts);
// Cuts [0, count) by cost into 64 parts per lane, whose boundaries the lanes find together: part i is
// [boundaries[i], boundaries[i + 1]). Each lane's order gets the parts costliest first, to within a factor of two. Like
// lane_sync, it waits for every lane.
static uint64_t lane_parts_by_cost(const uint64_t* cost_prefix, uint64_t count, uint64_t* boundaries, uint32_t* order);
// The next number this lane takes from its group's counter: 0, 1, 2, ... across the lanes, then parts or more.
static uint64_t lane_take(uint64_t parts);
// Runs entry(argument) on a group of count lanes, the calling thread being lane 0, and returns once they all have.
static bool lane_run(uint32_t count, void (*entry)(void* argument), void* argument, const uint32_t* cpus);
```

The lane lives in a thread-local context with the thread's scratch arenas, as
`TCTX` holds the RAD Debugger's `LaneCtx`. A thread that has to run a phase
alone, while the others do something else, makes itself a group of one (index
0, count 1, no barrier) with `lane_alone` and restores its group afterwards.
The barrier is a counter and a generation word: it spins briefly, waits in
`mwaitx`, then sleeps on a futex. Its atomics, and the counter `lane_take` adds
to, are the only ones the linker has: one atomic add per part, and none with one
lane.

Three things differ from the RAD Debugger, each measured below:

- **The calling thread is lane 0.** radbin's main thread waits for its lanes;
  here it is one of them, so a group of one starts no thread. A program starts
  its group once, with all of its phases in the entry point, since starting
  threads costs as much as 180 to 220 syncs.
- **A broadcast is one barrier.** `lane_sync_u64` copies the value in, waits,
  copies it out and waits again. Here the source offers its value on the
  barrier's arrival line, which it writes anyway as it arrives; the last lane to
  arrive copies it next to the generation word, and a waiting lane reads both
  together.
- **The barrier waits before it sleeps.** On Linux the RAD Debugger's barrier
  is `pthread_barrier_wait`, which sleeps in the kernel at every wait. Here a
  waiting lane spins for 128 pauses (1.5 µs on the 9700X), then waits in
  `mwaitx` for the generation word's line to be written, for up to 1 ms,
  before it sleeps ("How long to wait before sleeping" below). It sets a bit
  in the generation word first, so a sync at which no lane slept makes no
  system call.

The RAD Debugger's PDB converter balances uneven work with atomic counters that
lanes take tasks from, one task per take. Rule 1 below takes from a counter
too, but parts cut by cost, costliest first ("Balancing a phase" below).

The rules for every phase:

1. **Parts by cost, taken from a counter.** `lane_parts_by_cost` cuts a loop
   into 64 parts per lane, contiguous and in canonical order, each holding the
   items whose midpoints, in cost, fall in its share of the total. The costs
   are a prefix sum known before the phase: bytes for files, instructions for
   functions. The lanes take the parts from one counter with `lane_take`,
   costliest first to within a factor of two, until none are left. A lane that
   runs slower takes fewer parts, and a heavy item starts early rather than
   ending the phase late. A phase with less than about 64 µs of work per lane
   splits once instead, with `lane_range_by_cost`, since taking parts costs it
   2 to 7 µs on 8 lanes.
2. **Lanes write only what they own.** Shared memory is either read-only after
   a `lane_sync`, or split into ranges that one lane writes. There are no
   locks, and no two lanes write a cache line except at a range's edge.
3. **Count, place, fill.** Variable-sized output is counted per part, placed
   by prefix sums over the parts' counts that every lane computes for itself
   (cheaper than a broadcast, even at 512 values with 8 lanes), and filled into
   exactly sized ranges: each lane fills the parts it took.
4. **Exchange instead of sharing.** To bring together records that only a key
   relates (symbols by name, strings by content), each lane:
   1. counts its records per hash partition;
   2. after a sync, computes where its segment of each partition goes;
   3. writes its records there;
   4. after another sync, takes a range of partitions and processes their
      records, which come from all lanes in input order.

   This is a parallel radix partition, and mold's interning does the same
   (§4.1). Unlike mold's resolution and string merging (§4.2–4.3) and the RAD
   linker's hash trie, it needs no compare-and-swap. Records are 16 bytes (a
   hash, the input symbol and its precedence), and each crosses between cores
   once. Its first three steps split the records by lane (`lane_range`), so
   the segments' places follow from `lane_count()` counts per partition, where
   parts would need 64 times as many; the fourth takes parts of the
   partitions, by record count.
5. **Determinism.** The output depends on the inputs and options, never on the
   lane count or on scheduling:
   - the partition count depends on the input size, not on the lanes;
   - parts are contiguous and in canonical order, and output is placed by
     part, so neither which lane took a part nor how many parts there were
     shows;
   - ties break by input order;
   - padding is absolute (see "Layout");
   - every reduction is order-independent or done in canonical order.

   The tests run each phase with 1, 2, 3 and 8 lanes and compare the outputs
   byte for byte.
6. **Narrow only where it is cheap.** One lane (`lane_index() == 0`, then a
   sync) does what must happen once: opening the output, `ftruncate`, the
   segment table of a few dozen sections. Work every lane can repeat cheaply
   is repeated rather than broadcast. Fleury's point is that duplication can
   be cheaper than communication.

How the phases map onto lanes:

| Phase | Split | Lane-owned output | After the sync |
| --- | --- | --- | --- |
| Encoding (direct path) | parts of the functions, by instruction count | code buffer, with each part's offset in it; function sizes; fixups | each part's base: a prefix sum over the parts' sizes |
| Layout | parts of the atoms | the addresses of its atoms | each part's base: a prefix sum of the part totals |
| Patch and write | parts of the output, by bytes | its file ranges | the build ID's tree |
| Parse (foreign) | parts of the inputs, by bytes: a few large inputs are a part each, largest first | sections, symbols, name hashes | the exchange |
| Resolve | parts of the hash partitions, by record count | winners, written into each input's resolution array | — |
| Garbage collection and archive loading | frontier ranges | bitsets | OR-reduce into the next frontier |
| Merge strings | parts of the sections, then of the partitions | string offsets | — |

With one lane, every sync returns at once, a loop is one part, `lane_range`
returns everything, and the exchange's partitions are processed by one owner.
That is the first implementation, and it pays almost nothing for being written
this way. Where lanes help is measured, as is everything else:

- the exchange costs a second pass over the records;
- on btrfs, the file write doesn't scale.

**What the lanes cost.** `benchmark-lanes` on the 9700X, with lane i pinned to
CPU i (CPUs 0 to 7 are its eight cores). Each figure is the median of three
runs, each of them the median of 21 passes:

| Lanes | `lane_sync` | `lane_broadcast` | `pthread_barrier_wait` | Waking from `mwaitx` | Waking from the futex | Starting the group |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | 0.0 ns | 0.3 ns | 48 ns | — | — | — |
| 2 | 42 ns | 46 ns | 1.46 µs | 0.30 µs | 3.3–14 µs | 9.2 µs |
| 3 | 85 ns | 72 ns | 1.60 µs | 0.29 µs | 2.3–3.5 µs | 10 µs |
| 4 | 116 ns | 110 ns | 1.75 µs | 0.37 µs | 3.8–14 µs | 14 µs |
| 5 | 139 ns | 144 ns | 1.78 µs | 0.31 µs | 10–14 µs | 19 µs |
| 6 | 166 ns | 168 ns | 1.79 µs | 0.32 µs | 14 µs | 26 µs |
| 7 | 188 ns | 197 ns | 1.81 µs | 0.33 µs | 15 µs | 32 µs |
| 8 | 219 ns | 223 ns | 1.86 µs | 0.34 µs | 15 µs | 39 µs |

- **Sync and broadcast:** back-to-back calls, every lane arriving at once. Each
  lane adds about 29 ns, the cost of its atomic add moving the arrival line
  between cores. A broadcast costs what a sync does. The barrier before
  `mwaitx`, which spun 2,048 pauses before it slept, measured the same within
  8% either way, in runs alternating with these.
- **`pthread_barrier_wait`:** the RAD Debugger's barrier on Linux, in the same
  loop. It costs 8 to 35 times as much with 2 to 8 lanes.
- **Waking:** lane 0 arrives late, and the time is from its arrival until the
  last lane has left. 200 µs late, the others are waiting in `mwaitx`; 3 ms
  late, they are asleep on the futex, and waking them took 2 to 4 µs in some
  runs and 10 to 15 µs in others, as their cores had gone into a shallower
  idle state or a deeper one (the futex column has the three runs' range where
  they differed). This table had 2.0–3.4 µs for the
  futex before (10–15 µs in some runs), from lanes that had done nothing but
  sync before they slept 1 ms. Lanes that worked before they slept, as in
  "How long to wait before sleeping", took 10–15 µs to wake on the cores with
  the powersave governor, and 1.4–2.4 µs on CPU 2, whose governor is
  performance (see the appendix).
- **Starting:** `lane_run` of an entry point that returns at once, without
  pinning, from the call until it returns. That is what 180 to 220 syncs cost,
  hence one group per program. It costs 0.5 to 3 µs more than with the barrier
  before `mwaitx`: its passes now follow the ones in which the lanes sleep for
  3 ms.

Where the broadcast's value goes decided its cost (`benchmark-lanes`, medians
of four or five rotating runs of each layout):

| Layout | 4 lanes: sync / broadcast | 8 lanes: sync / broadcast |
| --- | ---: | ---: |
| Two slots on the generation line, written by the source | 102 / 141 ns | 204 / 284 ns |
| The counter, the generation and the slot on one line | 94 / 95 ns | 242 / 246 ns |
| The slot on a line of its own | 98 / 125 ns | 196 / 230 ns |
| Offered on the arrival line, copied next to the generation (`lane.c`) | 102 / 98 ns | 208 / 206 ns |

Writing a slot on the generation line makes the lanes spinning on it read the
line again, and putting the counter there too makes every arrival do so.

**How long to wait before sleeping.** A lane that waited long enough at a sync
runs slower afterwards, which costs every phase that follows a narrow section
or an unbalanced one. On the 9700X its clock drops to half, 2.77 GHz from
5.5, about 0.1 to 0.2 ms after it resumes, and comes back about 1 ms later, so
its next millisecond of work takes 25 to 60% longer. On CPU 2, the one core
with the performance governor, the clock drops to 3.77 GHz instead. What
counts is how long the core sat idle, not how the lane waited:

- lanes that spun never slowed down;
- lanes that waited in `mwaitx` didn't up to 1.5 ms, and did from 2 ms on;
- lanes that slept on the futex did from 0.7 ms on.

A harness measured it (`~/buster-fast-bench/lane-wake` on the 9700X). Eight
lanes pinned to CPUs 0–7 work for 2 ms. Then one lane, which rotates, works
alone for the wait while the others wait at a sync, in the way being tested.
Then they all work for 5 ms more, timing every 3 µs of work and reading APERF
and MPERF with `rdpru`. Each cell has two figures, medians over 48 trials of
the seven cores with the powersave governor:

- how much longer than at full speed their first millisecond of work after the
  wait took. Spinning lanes never slow down, so their 1.3–1.6% is the
  measurement's floor;
- how long after the late lane arrived they left.

| Wait | Futex after 2,048 pauses (before) | Spinning | `mwaitx` | `lane.c`: 128 pauses, `mwaitx` up to 1 ms, futex |
| ---: | ---: | ---: | ---: | ---: |
| 200 µs | 0.8%, 10 µs | 1.3%, 0.05 µs | 1.7%, 0.23 µs | 1.4%, 0.30 µs |
| 500 µs | 1.3%, 10 µs | 1.5%, 0.03 µs | 1.7%, 0.23 µs | 1.7%, 0.30 µs |
| 700 µs | 45%, 11 µs | 1.6%, 0.06 µs | 1.5%, 0.22 µs | 1.5%, 0.31 µs |
| 1 ms | 32%, 14 µs | 1.6%, 0.05 µs | 1.5%, 0.22 µs | 1.5%, 1.6 µs |
| 1.5 ms | 44%, 15 µs | 1.6%, 0.03 µs | 1.5%, 0.23 µs | 1.7%, 10 µs |
| 2 ms | 36%, 15 µs | 1.6%, 0.06 µs | 25%, 0.29 µs | 46%, 14 µs |
| 5 ms | 37%, 14 µs | 1.5%, 0.06 µs | 33%, 0.32 µs | 41%, 15 µs |
| 10 ms | 58%, 15 µs | 1.5%, 0.07 µs | 56%, 0.30 µs | 60%, 15 µs |

What the waiting costs meanwhile: the package's power while one lane works
and the others wait, back to back for 200 ms, and the median waiting core's
(RAPL's `energy-pkg` and `energy-core`, through perf). With every core idle
the package draws 20 W, and with all eight working 67 W:

| Wait | Futex (before) | Spinning | `mwaitx` | `lane.c` |
| ---: | ---: | ---: | ---: | ---: |
| 200 µs | 47 W, 2.4 W a core | 59 W, 4.2 W | 42 W, 2.0 W | 43 W, 2.1 W |
| 1 ms | 43 W, 2.0 W | 57 W, 4.0 W | 42 W, 2.0 W | 42 W, 2.0 W |
| 2 ms | 29 W, 0.13 W | 57 W, 4.0 W | 43 W, 2.0 W | 43 W, 1.9 W |
| 5 ms | 30 W, 0.09 W | 57 W, 4.0 W | 43 W, 2.0 W | 33 W, 0.59 W |
| 10 ms | 28 W, 0.07 W | 57 W, 4.0 W | 44 W, 2.2 W | 29 W, 0.17 W |

For waits up to 2 ms, the lane working alone ran at 5.45–5.48 GHz while the
others spun or waited in `mwaitx`, against 5.48–5.52 GHz while they slept:
they cost it about 1% of its clock.

So `lane.c` spins for 128 pauses, which a sync at which every lane arrives at
once never gets past, then waits in `mwaitx` for up to 1 ms, then sleeps on
the futex:

- **Waits up to 1.5 ms leave the lanes at full speed,** where the barrier
  before slowed them by 32–45% from 0.7 ms on. The linker's phases should
  wait less than that: the whole link of the 46 MB program is to take 4 to
  10 ms on one lane.
- **The lanes wake in 0.3 µs** rather than 10 to 15 µs.
- **Waiting in `mwaitx` draws what sleeping does for such waits,** 2 W a core,
  half what spinning does, and costs the lane working alone as much as
  spinning does.
- **Past 1 ms the lane sleeps.** From 2 ms on, every way of waiting but
  spinning slows the lanes afterwards, and spinning costs 4 W a core and holds
  cores the rest of the machine could use. So the futex saves the power, and
  by 10 ms a waiting core draws 0.17 W.
- **`monitorx` and `mwaitx` work in user mode** on Zen, and `mwaitx`'s timer
  bounds each wait. Its C1 hint wakes as fast as C0's (`0xf0`) and draws a
  little less.

`benchmark-lanes` ends with the same measurement for its largest group: lane 0
works alone for the wait, then every lane works for 1 ms. Medians of three
runs, against full speed, with the wake as in the table above:

| Wait | Wake | Lane 0 alone | Slowest lane after | Median lane after |
| ---: | ---: | ---: | ---: | ---: |
| 200 µs | 0.42 µs | +0.3% | +0.5% | +0.5% |
| 1 ms | 0.41 µs | +0.2% | +0.3% | +0.3% |
| 2 ms | 11 µs | 0.0% | +41% | +41% |
| 5 ms | 15 µs | −0.7% | +36% | +36% |

**Balancing a phase.** Static ranges from `lane_range_by_cost`, which rule 1
used before, balance as well as anything when every lane runs at full speed
and the items are many. But they wait for the slowest lane, and for a heavy
item that comes last. A harness on the 9700X (`~/buster-fast-bench/lane-balance2`,
from issue #75's `lane-balance`) ran phases of 8 ms of work at full speed on 8
lanes, with the items' costs estimated up front: heavy-tailed items
(lognormal, σ = 1.5), with exact estimates or estimates off by 0.5–2×, and
many equal items, off by 0.5–2×. Four ways to share them out:

- **Rule 1 before:** `lane_range_by_cost`.
- **`takecost64`:** 64 parts per lane, taken in index order, each finding its
  boundaries as it is taken.
- **`largest1`:** one item per take, largest first: the RAD Debugger's loop
  over the items sorted beforehand, untimed.
- **Parts, costliest first:** `lane.c`'s, with the boundaries and the order
  made in the phase.

Each cell is the efficiency (the time on one lane over 8 times the time),
first with every lane at full speed, then with lanes 4–7 sharing their cores
with a thread that runs a busy loop, then with lanes 4–7 asleep for 1 ms before
each phase. Each is the median of 21 passes, averaged over two runs:

| Workload | Rule 1 before | `takecost64` | `largest1` | Parts, costliest first |
| --- | ---: | ---: | ---: | ---: |
| 256 heavy-tailed items (32 a lane) | 90 / 89 / 68 | 81 / 81 / 70 | 98 / 97 / 87 | 97 / 97 / 81 |
| the same, estimates off by 0.5–2× | 78 / 77 / 62 | 75 / 75 / 65 | 98 / 97 / 81 | 97 / 97 / 84 |
| 2,048 heavy-tailed items | 96 / 95 / 74 | 94 / 93 / 76 | 97 / 96 / 85 | 97 / 96 / 82 |
| the same, estimates off | 89 / 88 / 68 | 93 / 92 / 77 | 97 / 96 / 85 | 97 / 96 / 82 |
| 16,384 heavy-tailed items | 98 / 93 / 74 | 96 / 94 / 79 | 89 / 87 / 76 | 97 / 94 / 83 |
| the same, estimates off | 95 / 91 / 71 | 96 / 94 / 81 | 89 / 87 / 75 | 97 / 94 / 84 |
| 65,536 items of 120 ns on average | 97 / 80 / 70 | 96 / 87 / 83 | 71 / 67 / 61 | 96 / 87 / 79 |
| 524,288 items of 15 ns on average | 97 / 67 / 65 | 94 / 79 / 80 | 9 / 9 / 9 | 95 / 80 / 76 |

- **One way covers both cases.** `largest1` beats parts taken in index order
  up to 512 items per lane, and loses from 1,024 (94% against 96% with 8,192
  heavy-tailed items): one take per item then costs more than the imbalance
  it saves. Taking the parts costliest first makes that threshold moot. They
  come within 0.5 points of `largest1` where it wins, and within 1.6 points of
  the static ranges at full speed everywhere else.
- **Uneven lanes are where parts pay:** 1 to 22 points over the static ranges.
  A thread sharing a core slows throughput-bound work the most, as the many
  small items are; the busy loop of the heavy items depends on itself and
  barely notices it.
- **Lanes that start the phase slow** (the third figure, a slowdown "How long
  to wait before sleeping" describes) cost parts 3 to 5 points more than
  `takecost64` on many small items. The lanes find the boundaries together,
  and the fast ones wait at that sync for the slow ones' share. With the
  barrier waiting in `mwaitx`, waits of up to 1.5 ms no longer start a phase
  slow.

`benchmark-lanes` measures the same on its largest group, with phases of
4 ms of work at full speed and costs of 2^(r mod 24), and with lanes 4–7
running every item twice. Each figure is the time's share of the best
possible: all the work at full speed, spread over the lanes as fast as each
can do it:

| Workload | Rule 1 before | Parts | Rule 1 before, lanes 4–7 at half speed | Parts, lanes 4–7 at half speed |
| --- | ---: | ---: | ---: | ---: |
| 256 heavy-tailed items | 89% | 99% | 59% | 99% |
| 16,384 heavy-tailed items | 99% | 98% | 66% | 98% |
| 65,536 equal items | 99% | 99% | 66% | 98% |

Parts cost a phase 2 to 7 µs on 8 lanes: the boundaries, a sync, the order
and 64 atomic adds per lane, the most when the takes come all at once. With
64 µs of work per lane, parts and static ranges tie at full speed (95–97%
against 89–99%). With 16 µs, parts lose 2 to 4 µs. With 4 µs, a phase takes
7–10 µs where static ranges take 2–5. Hence rule 1's limit.

The counter is a second atomic beside the barrier's. All 8 lanes share one CCD
on the 9700X. On a part with several, a contended take costs more, which 512
takes per phase on 8 lanes leave room for.

### SIMD: what pays

The rule is the encoder's: decide with measurements on the 9700X and explain
with uops.info's measured Zen 5 data.

Zen 5's numbers for the instructions in question:

| Instruction | Cycles (throughput) | Notes |
| --- | ---: | --- |
| `vpgatherdq` (8 qwords) | 6.1 | measured is slower than its port count: computed 4.5 |
| `vpgatherdd` (16 dwords) | 12.6 | |
| `vpscatterdd` (16 dwords) | 17 | |
| `vpscatterdq` (8 qwords) | 9 | |
| `vpcompressb` to memory | 3 | Zen 4's is microcoded at 168 |

Patching `PC32` fields, ns per fixup on CPU 2 (`benchmark-executable`, which
keeps every variant and checks each one's bytes against the kernel's):

| Variant | 64 KB, 2,000 fixups, 10,000 targets | 1 MB, 30,000, 100,000 | 64 MB, 2 M, 1 M |
| --- | ---: | ---: | ---: |
| scalar, 64-bit targets | 0.54 | 0.78 | 1.44 |
| gather (8), scalar stores | 0.42 | 0.59 | **1.38** |
| gather (8), scatter | 0.70 | 0.92 | 1.77 |
| scalar, 32-bit targets (`executable_patch_pc32`) | **0.37** | **0.53** | 1.40 |
| gather (16), scatter, 32-bit | 0.38 | **0.53** | 1.45 |

Gathers win back about 20% when targets are 64-bit. With 32-bit image offsets
the scalar loop is as fast as anything, and scatters tie or lose. The kernel
stays scalar with 32-bit targets, and the variants stay in the benchmark. (The
first measurements, in a harness before the representation, had the 8-wide
scatter at 1.20–1.89 ns.)

Where the decisions stand:

| Work | Bound by | Decision |
| --- | --- | --- |
| Encoding instructions | dependency chains (see the encoder document) | AVX-512 (done: 0.64 ns per instruction) |
| Patching fixups | loads and stores | scalar, 32-bit targets (done: `executable_patch_pc32`, measured above) |
| ELF records (`Rela`, `Sym`, dynamic tables) | stores | scalar (measured: ties the `vpermt2q` transpose) |
| Copying chunks | bandwidth | `memcpy`; the chunks stay in L2 |
| Hashing names | arithmetic | vectorized, one name per masked load, several in flight |
| Splitting strings at NULs | arithmetic | the lexer's bitmask-to-positions method |
| Decoding and classifying relocations | shuffles and tables | `vpermt2q` transpose, `vpermb` classification, `vpcompressd` lists |
| Bitsets (liveness, GOT needs) | bandwidth | 512-bit OR, `vpopcntq` for counts |
| Prefix sums for layout | latency | scalar until a profile says otherwise |
| Build ID | arithmetic | AES rounds on each chunk while it is in L2 (done: 260 GB/s, as fast as reading it; "Patching and writing") |
| Probing hash tables | memory latency | scalar loads with software prefetch: gathers measured no faster than scalar loads in the patch kernel |

So on the direct path, most of the output is produced by a vector kernel that
already exists (the encoder), and what is left for the linker is load/store
and page-cache work, where the floors above say what is possible. The foreign
path is where new vector kernels earn their place.

## Correctness

The tests live in `build/compiler`, as the lexer's and the encoder's do:

- **Fixup offsets and addends** against GNU as's relocation records for the
  same instructions with symbolic operands, over random streams of every form
  that takes a displacement or an immediate.
- **Generated programs** (`link_program.c`), which every later step's tests
  and benchmarks use, of two kinds. Static programs without libc, whose
  `_start` sets up TLS from `PT_TLS` and runs the init and fini arrays, are
  step 4's first executables. Programs whose `main` the C runtime calls are
  step 5's: they import `write`, `strlen`, `environ` and `__errno_location`
  from libc through the GOT, as "Binding before instruction selection" says,
  keep a pointer to `strlen` in their data, and check that their TLS block
  and libc's don't overlap. Their workers call each other directly and
  through the GOT, read globals directly and through the GOT, use local-exec
  and initial-exec TLS, and read string literals. Their data holds pointers,
  wide addends among them, and both kinds read a weak symbol that nothing
  defines through its GOT slot. Every class appears given two thread-locals,
  an init function and a fini function, and every fixup kind, but options
  leave out the absolute ones (for a PIE) and `SIZE32` and `SIZE64` (wild 0.10
  refuses them, as linking each kind with GNU ld, lld, mold and wild on the
  9700X showed). The program prints a checksum of what it read, which a model
  of it predicts. The sizes come from options and the rest from a seed, so a
  benchmark can scale them up. `test-link` lays them out in memory as a
  static executable at a fixed address, patches every fixup, and runs them in
  a child process, which does what the C runtime does around `main` and gives
  libc's functions stand-ins: a reference for the direct path's patch kernel.
  `test-link --print-assembly` writes them as GNU as source, with a `.reloc`
  for each fixup. On the 9700X, GNU ld, lld, mold and wild link the objects,
  statically or through `cc` with libc (as a PIE, with `-no-pie` and with
  `-static`), into programs that print what the model predicts.
- **Objects:**
  - read back with `readelf` and `llvm-readobj` in the tests' cross-check
    mode, and compared with GNU as's objects of the same units
    (`test-link --print-assembly`);
  - linked by GNU ld, lld, mold and wild;
  - run, with their output compared.

  This is the object writer's equivalent of the encoder's GNU as cross-check.
- **Direct executables against linked ones:** the same generated program
  linked directly and through the object writer and lld must behave the same,
  and must have the same symbols, segments, dynamic entries and relocations up
  to addresses.
- **Rules other linkers follow,** each against GNU ld's and lld's output for a
  small program:
  - a pointer at an odd address stays in `.rela.dyn`;
  - a weak reference loads no archive member;
  - a common symbol beats a weak definition;
  - a wide string section keeps its strings;
  - a definition a shared library refers to is exported;
  - a local-exec TLS reference in an object is a `TPOFF32`, not a frozen
    offset.
- **The patch kernel:** its fast path against the checked one, on random
  fixups of every kind with wide addends among them, at every count from 0 to
  199, with the kind column ending at a `PROT_NONE` page, as the lexer's and
  the encoder's tests place their buffers (`test-executable`). The benchmark
  checks every variant's bytes against the kernel's.
- **The direct path's executables:** `test-executable` writes test-link's
  static programs and runs them, checks every field against its kind's
  arithmetic, the GOT's slots, the headers against lld's layout, the chunk
  boundaries and the build ID, and the output file's cases: in place, created,
  renamed over a running program, another link, someone else's file or a
  symbolic link, written in order into a FIFO (opened once, for writing, and
  one that may be written but not read), a directory, and what a failed link
  leaves.
  It checks the refusals (each reported alike with any lane count) and a
  lane short of scratch memory at every amount, from none up.
- **Lane counts:** each phase with 1, 2, 3 and 8 lanes, outputs compared byte
  for byte. `test-executable` writes every program so, each time from zeroed
  fields over the last file scribbled over, so that a lane that skipped a
  field or a chunk shows.
- **Foreign links,** against lld and mold on the inputs above and a larger
  corpus: same exit status and output when run, the same set of symbols with
  the same sizes, and `readelf` checks of the dynamic section, versions and
  relocations.

Invalid input (an object with an out-of-range offset, an undefined symbol, a
relocation that doesn't fit) is a diagnostic naming the input, symbol and
offset, never an assertion.

## Incremental output

Later, and only after the direct path. With a stable layout (each function
keeps its slot while it fits, in a development mode that leaves room), a
rebuild after an edit:

1. re-encodes the changed functions;
2. patches their own fixups, and the fixups that point at anything that
   moved, from a reverse index (each target's incoming fixups, built by a
   counting sort on `target`);
3. rewrites only the pages that changed, in place.

It stays correct by construction:

- When ETXTBSY forbids writing in place, a copy-on-write clone (`FICLONE` on
  btrfs and XFS) of the old output takes the patches and is renamed over the
  output. Where cloning is unavailable, a full copy is the fallback, and its
  cost counts.
- Anything outside the fast path falls back to a full link: a change of
  binding, a COMDAT winner, garbage-collection roots, TLS layout, the dynamic
  symbols or options.
- A non-incremental build lays out canonically, so its output stays
  reproducible.

The compiler knows exactly which functions changed, so nothing has to be
diffed after the fact, which is the part wild's incremental design (Lattimore,
2024) spends most of its machinery on. The mold paper argues against
incremental linking (§10) because the bookkeeping costs every link and changes
often force a full one. Both arguments are measured here before anything is
built.

## Plan

Each step is a pull request with tests and a benchmark in `build/compiler`,
and with this document updated with what was measured:

1. **The lane base layer:** the lane context, barrier, `lane_range`, broadcast
   and arenas with huge pages. Tests run with several lane counts even though
   the linker starts with one. Done: `lane.c`, `test-lanes` and
   `benchmark-lanes`, whose costs are in "Lanes".
2. **Fixups from the encoder:** marked instructions, field offsets and
   addends. Done when they match GNU as's relocations on random streams, and
   the compiler-like mix encodes no slower than the offsets loop (0.72 ns per
   instruction).

   Then what steps 3 to 5 build on: the representation (`link.c`: the
   tables in "Representation", and fixups from the encoder's marked
   instructions) and generated test programs (`link_program.c`). Done:
   `test-link`.
3. **The object writer:** code, data, symbols and relocations from generated
   programs. Done when GNU ld, lld, mold and wild link its objects into
   programs that run, `readelf` agrees, and `benchmark-object-writer` reports
   per-object costs against the file floor.
4. **The direct path, starting without libc:** layout, patching, the file
   write and the build ID, which every executable written straight from the
   compiler's state goes through. Linking libc needs step 5's runtime
   snapshot, so the first executables are static ones whose `_start` makes
   system calls, with no libc or crt objects. Done when the tests' programs
   run. Done: `executable.c`, `test-executable` and `benchmark-executable`
   ("The first executables"): the tests' programs run, written byte for byte
   alike by 1, 2, 3 and 8 lanes.
5. **The runtime snapshot and the dynamic PIE:** the direct path for programs
   that call libc: binding, GOT, RELR, versions, the snapshot's validation and
   rebuild. Done when generated programs that call libc run, match their
   objects linked by lld, and `benchmark-link` meets the targets in "Summary".

   The comparison has to be between outputs with the same features. The
   generated programs carry a CIE and an FDE per function, so this step
   writes `.eh_frame` and `.eh_frame_hdr`, a build ID, and binds at load time.
   It is measured against the baselines with matched options
   (`--eh-frame-hdr`, `--build-id`, `-z now`, no garbage collection). DWARF
   waits for step 9, which is measured against the `-g` baselines.

   `benchmark-link` replays one generated program through the direct path,
   and through the object writer followed by mold, wild and lld. Besides
   time, it reports the work done: bytes written, fixups applied, names looked
   up, page faults and peak memory.
6. **`-static`:** libc.a's closure, IRELATIVE for its IFUNCs, static TLS.
   Then `-static-pie` and `-shared`.
7. **Foreign inputs:** objects, archives, shared libraries, the vector parse
   and the exchange. Done when the differential tests against lld and mold
   pass on a corpus.
8. **More than one lane:** measured scaling, and the per-filesystem write
   choice.
9. **Unwind tables from the code generator's prologues, and DWARF,** then
   **streaming the output** in the data-first order, measured against writing
   at the end.
10. **Incremental output,** if the measurements in 9 leave it worth doing.

## Sources

- Rui Ueyama, *mold: A Massively Parallel Linker*, arXiv 2608.23228 (2026):
  - the pipeline, §3–4;
  - huge pages, `fallocate` and overwriting executables in place, §5;
  - the single-thread run, §7.3, Table 7;
  - the per-pass ablation, §7.4, Table 8.
- David Lattimore's wild:
  - `DESIGN.md` and `BENCHMARKING.md`;
  - "Designing Wild's incremental linking" (2024-11-19);
  - "Benchmarking Wild vs Mold" (2026-09-18), on how much fork, tmpfs and
    reusing the output change results.
- The RAD Linker, in the RAD Debugger repository (`src/linker`):
  - a `TP_` thread pool;
  - per-worker chunk lists;
  - count, prefix-sum and fill;
  - deterministic sorts by input index;
  - the image built in memory and written by a background thread;
  - large pages for 25% on Windows.
- Ryan Fleury, "Multi-Core By Default", Digital Grove (2025-10-10), and the RAD
  Debugger's `src/base/base_thread_context.h`.
- uops.info's Zen 5 measurements for the gather, scatter and compress
  instructions above.
- The measurements on the 9700X: the appendix has the inputs, the exact
  commands and the methods. The harness becomes `benchmark-link` and the floor
  commands in step 5, inside `build/compiler` like every other benchmark.

## Appendix: how the numbers were measured

**The machine.** A Ryzen 7 9700X (Zen 5) running Arch Linux with kernel
6.18.50-2-lts:

- 15 CPUs online (CPU 10, CPU 2's SMT sibling, is offline);
- amd-pstate-epp in active mode with the performance governor for the link
  measurements. For the lanes' (2026-10-19), only CPU 2 had it, and the others
  ran the powersave governor with the energy preference
  `balance_performance`;
- tmpfs at `/tmp` and btrfs (zstd:3) at `/home`;
- glibc 2.44, GCC 16.2.1's crt files and libgcc, clang 23.1.1;
- GNU ld 2.47, lld 23.1.1, mold 3.0.0 and wild 0.10.0.

Every run took the machine's lock (`~/buster-fast-bench/exclusive.sh`), one
run per lock. One-thread runs are pinned with `taskset -c 2`. The scripts are
in `~/buster-fast-bench/linker-baseline/` there.

**Inputs.**

- **hello world:** `hello.c`, a `main` that calls `puts("hello")`, compiled
  with `clang -O2 -c`.
- **tcc:** tinycc at 43c7708b, built with `./configure --cc=clang` and
  `make ONE_SOURCE=no tcc`. The `-g` build adds
  `--extra-cflags="-O2 -g -Wall"` to `configure`. Its link takes `tcc.o
  libtcc.a -lm -ldl -lpthread`.
- **synth:** the script below, run with `512 200 synth_src`. Each file is
  compiled with `clang -O2 -c`, or `clang -O2 -g -c` for the `-g` build,
  without `-march`.

```python
#!/usr/bin/env python3
# Generates a synthetic C program for link benchmarks: FILES translation units of FUNCS functions each, with calls
# across files (PLT32), extern data (GOTPCRELX/PC32), function pointer tables (R_X86_64_64 -> RELATIVE in a PIE),
# string literals (mergeable .rodata.str1.1) and libc calls. main() runs f_0_0(argc - 1), which takes no branch for
# argc == 1, so the linked program can be run to check the link.
import random
import sys
import os

files = int(sys.argv[1])
funcs = int(sys.argv[2])
out = sys.argv[3]
rng = random.Random(12345)
os.makedirs(out, exist_ok=True)

def fname(f, i):
    return f"f_{f}_{i}"

def gname(f, i):
    return f"g_{f}_{i}"

GLOBALS = 8
for f in range(files):
    lines = ["#include <stdio.h>", "#include <string.h>"]
    body = []
    for i in range(funcs):
        callees = [(rng.randrange(files), rng.randrange(funcs)) for _ in range(3)]
        data = [(rng.randrange(files), rng.randrange(GLOBALS)) for _ in range(2)]
        for (cf, ci) in callees:
            if cf != f:
                lines.append(f"extern int {fname(cf, ci)}(int);")
            else:
                lines.append(f"int {fname(cf, ci)}(int);")
        for (df, di) in data:
            if df != f:
                lines.append(f"extern long {gname(df, di)};")
        (c0, c1, c2) = callees
        (d0, d1) = data
        k = rng.randrange(1, 1000)
        body.append(f"""int {fname(f, i)}(int x)
{{
    int acc = x;
    for (int i = 0; i < (x & 7); i++) acc = acc * {k} + i;
    if (acc & 1) acc += {fname(*c0)}(acc >> 1);
    if (acc & 2) acc ^= {fname(*c1)}(acc + 3);
    acc += (int){gname(*d0)};
    {gname(*d1)} += acc;
    if (acc == {k * 7 + 12345}) printf("{fname(f, i)} %d %s\\n", acc, "{rng.choice(['alpha', 'beta', 'gamma', 'delta'])}");
    if (acc & 4) acc += table_{f}[acc & 3](acc);
    if (acc & 8) {{ char buf[32]; memset(buf, acc, sizeof(buf)); acc += buf[x & 31]; }}
    return acc + ((acc & 16) ? {fname(*c2)}(acc) : 0);
}}
""")
    for g in range(GLOBALS):
        lines.append(f"long {gname(f, g)};")
    lines.append(f"static int (*table_{f}[4])(int);")
    lines.extend(body)
    lines.append(f"static int (*table_{f}[4])(int) = {{ {', '.join(fname(f, rng.randrange(funcs)) for _ in range(4))} }};")
    with open(os.path.join(out, f"s{f}.c"), "w") as handle:
        handle.write("\n".join(lines) + "\n")

with open(os.path.join(out, "main.c"), "w") as handle:
    handle.write("#include <stdio.h>\nint f_0_0(int);\nint main(int argc, char** argv)\n{\n    (void)argv;\n    printf(\"%d\\n\", f_0_0(argc - 1));\n    return 0;\n}\n")
```

**Link commands.** These are the clang driver's (`clang -###`) with the linker
swapped. `GCC` is `/usr/lib/gcc/x86_64-pc-linux-gnu/16`, and the options in
brackets are the matched ones:

```sh
LINKER --hash-style=gnu --build-id --eh-frame-hdr -m elf_x86_64 [-z now --no-gc-sections] \
    -pie -dynamic-linker /lib64/ld-linux-x86-64.so.2 -o OUT \
    /usr/lib/Scrt1.o /usr/lib/crti.o $GCC/crtbeginS.o -L$GCC -L/usr/lib INPUTS \
    -lgcc --as-needed -lgcc_s --no-as-needed -lc -lgcc --as-needed -lgcc_s --no-as-needed \
    $GCC/crtendS.o /usr/lib/crtn.o
```

For `-static`:

```sh
LINKER --hash-style=gnu --build-id --eh-frame-hdr -m elf_x86_64 [-z now --no-gc-sections] -static -o OUT \
    /usr/lib/crt1.o /usr/lib/crti.o $GCC/crtbeginT.o -L$GCC -L/usr/lib hello.o \
    --start-group -lgcc -lgcc_eh -lc --end-group $GCC/crtend.o /usr/lib/crtn.o
```

- `LINKER` is `ld.bfd`, `ld.lld`, `mold` or `wild`.
- One-thread runs add `--threads=1` (lld) or `--no-threads` (mold, wild).
- `--build-id` without a style is each linker's default.

**Timing.** A harness forks and execs the linker, with stdout and stderr on
`/dev/null`, and waits for it.

| Runs | Pause between runs |
| --- | --- |
| 41, after one warm-up | 20 ms |
| 11 for synth | 200 ms |
| 5 for GNU ld on synth | 500 ms |

- The median of the wall time from `fork` to `waitpid`.
- The output path is the same for every run, so the file exists.
- mold's and wild's forking parents return once the output is written.

The phase breakdowns come from `mold --perf --no-fork` and `wild --time
--no-fork`, and the counters from `perf stat -r 11 -e
task-clock,page-faults,context-switches,cycles,instructions`, all with one
thread.

**All baselines with matched options** (`-z now --no-gc-sections`), ms:

| Program | GNU ld | lld | mold | wild | lld, 1 thread | mold, 1 thread | wild, 1 thread |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| hello | 6.01 | 6.01 | 5.32 | 2.52 | 5.02 | 2.44 | 1.34 |
| hello, `-static` | 14.11 | 8.84 | 8.58 | 3.97 | | | |
| tcc | 10.56 | 6.89 | 6.82 | 2.84 | 5.78 | 3.26 | 2.15 |
| tcc, `-g` | 12.63 | 7.81 | 7.93 | 3.11 | | | |
| synth | 343.5 | 58.4 | 38.0 | 25.9 | 106.9 | 96.4 | 140.0 |
| synth, `-g` | 480.7 | 77.5 | 49.7 | 43.3 | 164.1 | 146.1 | 236.6 |

**File and memory floors.** One thread on CPU 2, medians of 15 runs (5 at 64 MB
and above).

- Between runs at 64 MB and above, a `syncfs`, outside the timed region.
- The source is an anonymous buffer with `MADV_HUGEPAGE`, filled before
  timing.
- Each method's time includes `open` and `close`.

| Method | Calls |
| --- | --- |
| New file | `unlink`, `open(O_CREAT \| O_WRONLY \| O_TRUNC)`, `pwrite` until done, `close` |
| `O_TRUNC` | the same without `unlink` |
| In place | `open(O_WRONLY)`, `pwrite`, `ftruncate` to the same size, `close` |
| Temporary file | `open` of another name, `pwrite`, `close`, `rename` |
| `mmap` | `unlink`, `open(O_RDWR \| O_CREAT)`, `ftruncate`, `mmap(MAP_SHARED)`, `memcpy`, `munmap`, `close` |
| `mmap` with `fallocate` | the same with `fallocate` instead of `ftruncate`, and `MADV_HUGEPAGE` on the mapping |
| `mmap` in place | without `unlink` |

- **Several threads:** N threads pinned to CPUs 0 to N−1, released by a barrier
  once the file is open and sized (creating them isn't timed), each writing a
  contiguous Nth of the file.
- **Teardown:** a child touches N MiB of anonymous memory, tells the parent
  through a pipe and exits. The time is from the message to `waitpid`'s
  return.
- **Small calls:** 1,000 of each, timed together.

**Kernel prototypes.**

- Field offsets are sorted, one per stride (the buffer's size over the fixup
  count), at a random place within it, and targets are random.
- Every variant's output was compared byte for byte with the scalar loop's.
- Each time is the median of 11 timings of repeated calls.

The two 32-bit variants:

```c
// S + A - P into each 4-byte field. target_offsets are 32-bit image offsets; buffer_offset is the buffer's.
static void patch_pc32_scalar(uint8_t* buffer, uint32_t buffer_offset, const uint32_t* target_offsets,
                              const uint32_t* offsets, const uint32_t* targets, const int32_t* addends, uint64_t count)
{
    for (uint64_t i = 0; i < count; i += 1)
    {
        uint32_t value = target_offsets[targets[i]] + (uint32_t)addends[i] - (buffer_offset + offsets[i]);
        memcpy(buffer + offsets[i], &value, 4);
    }
}

static void patch_pc32_gather_scatter(uint8_t* buffer, uint32_t buffer_offset, const uint32_t* target_offsets,
                                      const uint32_t* offsets, const uint32_t* targets, const int32_t* addends,
                                      uint64_t count)
{
    __m512i base = _mm512_set1_epi32((int)buffer_offset);
    uint64_t i = 0;
    for (; i + 16 <= count; i += 16)
    {
        __m512i fields = _mm512_loadu_si512(offsets + i);
        __m512i s = _mm512_i32gather_epi32(_mm512_loadu_si512(targets + i), target_offsets, 4);
        __m512i value = _mm512_sub_epi32(_mm512_add_epi32(s, _mm512_loadu_si512(addends + i)),
                                         _mm512_add_epi32(base, fields));
        // The fields are disjoint, so the order the scatter writes them in doesn't matter.
        _mm512_i32scatter_epi32(buffer, fields, value, 1);
    }
    patch_pc32_scalar(buffer, buffer_offset, target_offsets, offsets + i, targets + i, addends + i, count - i);
}
```

The 64-bit variants:

- gather eight targets per iteration with `vpgatherdq`;
- check the int32 range by narrowing with `vpmovqd` and widening back with
  `vpmovsxdq`;
- store through a small array and scalar stores, or with `vpscatterdd`.

The `Elf64_Rela` writers fill 24-byte records from four 32-bit columns
(offset, symbol, type, addend): scalar, or three `vpermt2q` and three masked
`vpermq` per eight records.
