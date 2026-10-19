# The object writer and the linker

This is the design of the two components that turn the compiler's encoded
machine code and data into files:

- **The object writer:** ET_REL objects, for `-c` and for other linkers.
- **The linker:** executables written straight from the compiler, and links of
  other compilers' objects, archives and shared libraries.

Nothing here is implemented yet. The document records the measurements on the
Ryzen 7 9700X that the design rests on, the design itself, and the order to
build it in, as `docs/x86-64-encoder.md` does for the encoder.

The premise is that most of what mold and wild do on every link is work this
compiler can avoid altogether. What remains is memory traffic, and the design
can bring that close to what the kernel needs to put a file in the page cache.

## Summary

- **The fastest link is the one that doesn't happen.** Without `-c`, the
  compiler:
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
  - every thread runs the same code, splitting work with `lane_range` and
    meeting at `lane_sync`;
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
the output.

**Names** (`NameId`) are spellings interned by the front end. Each has its
bytes in the name arena and a 64-bit hash computed once, when the name is
interned. Local labels, `static` functions and string literals never need
their name to be looked up: references carry ids.

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
| `size` | its size |
| `bytes` | its contents: in a lane's buffer, the runtime snapshot or a mapped input; null for zero-fill |
| fixup range | where its fixups are |
| `address` | after layout: an image offset for a loaded atom, a file offset otherwise |

**Symbols** (`SymbolId`) are what binding is about. One atom can define
several, and several definitions can compete for one name. Columns:

| Column | Contents |
| --- | --- |
| `name` | its `NameId` |
| `atom`, `value` | the defining atom and the offset in it |
| `size` | its size |
| `flags` | binding, visibility, type |
| `source` | where it is defined: program, runtime shared library, runtime archive member, foreign object, foreign shared library, or the linker |
| `got_slot`, `dynamic_index`, `version` | its GOT slot, `.dynsym` index and version |
| `reference` | after layout: what code that refers to it uses (its address, its GOT slot's for an import, its offset in the TLS block for a TLS variable) |

`reference` is the only symbol column the patch kernel reads. It is 32-bit,
because a small-code-model image (everything a PC32 field can reach) is under
2 GiB, and the measured 32-bit kernel is the fastest (see "SIMD").

**Fixups** are references the encoder couldn't fill in. They are sorted by
atom and offset, 13 bytes each in columns:

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
| `CALL` | `call` or `jmp rel32` | `R_X86_64_PLT32` | S + A − P |
| `CALL_GOT` | `call` or `jmp [rip + slot]` (`ff 15`, `ff 25`) | `R_X86_64_GOTPCRELX` | G + A − P |
| `LOAD_GOT` | `mov reg, [rip + slot]` | `R_X86_64_REX_GOTPCRELX`, or `R_X86_64_GOTPCRELX` without REX | G + A − P, or, when S turns out to be local, S + A − P with `mov` rewritten to `lea` |
| `PC32` | any other RIP-relative operand | `R_X86_64_PC32` | S + A − P, checked to fit in int32 |
| `TPOFF32` | `fs:[x@tpoff]` (local-exec) | `R_X86_64_TPOFF32` | S's offset from the end of the TLS block + A |
| `GOTTPOFF` | `mov reg, [rip + x@gottpoff]` (initial-exec) | `R_X86_64_GOTTPOFF` | G + A − P, with a `TPOFF64` for the slot |
| `ABS64` | 8 bytes of data | `R_X86_64_64` | S + A, and in a PIE a relative relocation (a symbolic one for an import) |
| `PC64` | 8 bytes (unwind tables) | `R_X86_64_PC64` | S + A − P |
| `ABS32`, `ABS32S` | 4 bytes, non-PIE only | `R_X86_64_32`, `R_X86_64_32S` | S + A, zero- or sign-extension checked |
| `SIZE32`, `SIZE64` | 4 or 8 bytes | `R_X86_64_SIZE32`, `R_X86_64_SIZE64` | the size of S + A |

G is the address of S's GOT slot.

**Wide addends.** Any kind can carry a 64-bit addend: a bit in `kind` sends
the patch to a side table that holds it. Initializers like
`(char*)&x + 0x100000000` need it, and so do foreign `R_X86_64_PC64` and
`R_X86_64_64` relocations with large addends, which GNU as and ld accept.
The common case stays 13 bytes.

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

### The direct path (no `-c`)

When every input is a C source compiled in this process (one or many
translation units, a unity build or not), the executable is written straight
from the compiler's state. Foreign `.o` and `.a` inputs fall back to the
import path (see "Linking other compilers' objects"), which produces the same
representation, so a link can mix both.

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

After layout, each lane walks its own output ranges in file order, a chunk at
a time. Chunks are about 256 KB, a quarter of Zen 5's L2.

**Chunk boundaries never split a field.** They are computed from the image and
its fixups alone:

- a chunk ends at an atom's end, since no field crosses one;
- an atom larger than a chunk is cut where no field crosses.

A 4-byte field two bytes before a 256 KB boundary is ordinary (GNU as puts a
`call`'s at `0x3fffe` after 262,141 bytes of padding). A lane patching such a
field could otherwise change bytes the next lane had already hashed or
written. Because the boundaries don't depend on the lane count, the build ID's
chunks don't either. The lane tests place 4- and 8-byte fields across the
nominal boundaries.

For each chunk the lane:

1. applies the chunk's fixups (sorted by offset already);
2. hashes it for the build ID;
3. writes it.

The chunk is pulled into L2 by the first step and written from L2, where
`pwrite` into an existing page reaches 41 GB/s on tmpfs. Each byte of the
output moves once from the lane's buffer, and the fixup columns are read in
order.

The patch loop reads, per fixup:

- the field offset;
- the target's 32-bit image offset, from a table that stays in L2 up to about
  250,000 symbols;
- the addend.

Then it computes S + A − P and stores 4 bytes. It measures 0.39–0.55 ns per
fixup in L1 and L2, 0.56–0.79 ns in L3 and 1.4 ns from DRAM (see "SIMD"):
1.02 million fixups take 0.5–1.4 ms.

**The build ID** is a hash of every chunk, combined in a tree in chunk order,
so the lanes compute it without depending on each other and the result doesn't
depend on the lane count. Its note is written last.

GNU ld's default style, SHA-1, and the others it offers (md5, uuid, an
explicit value, none) are honored when asked for. The direct path's own
default is a fast 128-bit hash, decided by measurement.

**Writing the file:**

1. If the output exists, is a regular file with one link and isn't running,
   open it with `O_RDWR` and overwrite it in place. Then `ftruncate` it to the
   new size and set its mode. In place is 1.2–3.6× faster than a new file
   (table above). mold and wild do the same for executables.
2. If opening it fails with ETXTBSY (the program is running), or the file has
   other hard links, or the output is a shared library (which running
   processes map without ETXTBSY to protect them): write a new file with
   `O_CREAT | O_EXCL` beside it and rename it over the old one.
3. Never `O_TRUNC`: on btrfs it costs 58 µs for 16 KB and 10.4 ms for 4 MB.
4. **One lane writes with `pwritev`.** On tmpfs that is 6.8–12.6 GB/s for a
   new file and 11.7–41.6 GB/s in place; on btrfs, 4.4–7.5 and 6.4–9.4.
   **Several lanes copy into a shared mapping** of the file, which
   scales on tmpfs (14–19 GB/s with eight) and not on btrfs. So the choice
   goes by `statfs`'s filesystem type, from measurements per filesystem,
   including whether `fallocate` and `MADV_HUGEPAGE` help (they did on mold's
   ext4 and didn't here).
5. Close the file before returning: `execve` of a file open for writing fails
   with ETXTBSY. Don't `fsync`, like every other linker.

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
loop. The RAD Debugger has the same in its base layer (`lane_idx()`,
`lane_count()`, `lane_sync()`, `lane_sync_u64()`, `lane_range()` in
`base_thread_context.h`). In this repository's style:

```c
typedef struct Range64 Range64;
struct Range64
{
    uint64_t begin;
    uint64_t end;
};

// The calling thread's lane: its index, the group's size, and the group's barrier and broadcast slot.
static uint32_t lane_index(void);
static uint32_t lane_count(void);
// Waits for every lane of the group. With one lane, returns at once.
static void lane_sync(void);
// Every lane gets source_lane's value.
static uint64_t lane_broadcast(uint64_t value, uint32_t source_lane);
// This lane's part of [0, count): contiguous, in lane order, the first count % lane_count() parts one longer.
static Range64 lane_range(uint64_t count);
// The same, split so that each part's cost (from the inclusive prefix sum of the items' costs) is about equal.
static Range64 lane_range_by_cost(const uint64_t* cost_prefix, uint64_t count);
```

The lane lives in a thread-local context with the thread's scratch arenas, as
`TCTX` holds the RAD Debugger's `LaneCtx`. A thread that has to run a phase
alone, while the others do something else, makes itself a group of one (index
0, count 1, no barrier) and restores its group afterwards. The barrier is a
counter and a generation word: it spins briefly, then waits on a futex. Its
atomics are the only ones the linker has.

The rules for every phase:

1. **Ranges, not tasks.** Work is split with `lane_range` or
   `lane_range_by_cost`, in canonical order: lane 0's part is first. The cost
   prefix is what keeps uneven inputs balanced without a shared work counter:
   bytes for files, instructions for functions.
2. **Lanes write only what they own.** Shared memory is either read-only after
   a `lane_sync`, or split into ranges that one lane writes. There are no
   locks, and no two lanes write a cache line except at a range's edge.
3. **Count, place, fill.** Variable-sized output is counted per lane, placed by
   prefix sums over `lane_count()` values that every lane computes for itself
   (cheaper than a broadcast), and filled into exactly sized ranges.
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
   once.
5. **Determinism.** The output depends on the inputs and options, never on the
   lane count or on scheduling:
   - the partition count depends on the input size, not on the lanes;
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
| Encoding (direct path) | functions, by instruction count | code buffer, function sizes, fixups | each lane's base: a prefix sum over `lane_count()` sizes |
| Layout | atoms | the addresses of its atoms | each lane's base: a prefix sum of the lane totals |
| Patch and write | output chunks, by bytes | its file ranges | the build ID's tree |
| Parse (foreign) | inputs, by bytes | sections, symbols, name hashes | the exchange |
| Resolve | hash partitions | winners, written into each input's resolution array | — |
| Garbage collection and archive loading | frontier ranges | bitsets | OR-reduce into the next frontier |
| Merge strings | sections, then partitions | string offsets | — |

With one lane, every sync returns at once, `lane_range` returns everything,
and the exchange's partitions are processed by one owner. That is the first
implementation, and it pays almost nothing for being written this way. Where
lanes help is measured, as is everything else:

- the exchange costs a second pass over the records;
- on btrfs, the file write doesn't scale.

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

Patching `PC32` fields, ns per fixup on CPU 2:

| Variant | 64 KB, 2,000 fixups, 10,000 targets | 1 MB, 30,000, 100,000 | 64 MB, 2 M, 1 M |
| --- | ---: | ---: | ---: |
| scalar, 64-bit targets | 0.55 | 0.79 | 1.44 |
| gather (8), scalar stores | 0.45 | 0.60 | 1.37 |
| gather (8), scatter | 1.20 | 1.30 | 1.89 |
| scalar, 32-bit targets | **0.39** | 0.56 | 1.36 |
| gather (16), scatter, 32-bit | **0.37** | **0.52** | 1.42 |

Gathers win back about 20% when targets are 64-bit. With 32-bit image offsets
the scalar loop is as fast as anything, and scatters lose everywhere. The
kernel stays scalar with 32-bit targets, and the variants stay in the
benchmark.

Where the decisions stand:

| Work | Bound by | Decision |
| --- | --- | --- |
| Encoding instructions | dependency chains (see the encoder document) | AVX-512 (done: 0.64 ns per instruction) |
| Patching fixups | loads and stores | scalar, 32-bit targets (measured above) |
| ELF records (`Rela`, `Sym`, dynamic tables) | stores | scalar (measured: ties the `vpermt2q` transpose) |
| Copying chunks | bandwidth | `memcpy`; the chunks stay in L2 |
| Hashing names | arithmetic | vectorized, one name per masked load, several in flight |
| Splitting strings at NULs | arithmetic | the lexer's bitmask-to-positions method |
| Decoding and classifying relocations | shuffles and tables | `vpermt2q` transpose, `vpermb` classification, `vpcompressd` lists |
| Bitsets (liveness, GOT needs) | bandwidth | 512-bit OR, `vpopcntq` for counts |
| Prefix sums for layout | latency | scalar until a profile says otherwise |
| Build ID | arithmetic | a vector hash on each chunk while it is in L2 |
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
- **Objects:**
  - read back with `readelf` and `llvm-readobj` in the tests' cross-check
    mode;
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
- **The patch kernel:** every variant against the scalar reference, at every
  count around a group's size, between `PROT_NONE` guard pages as the lexer's
  and the encoder's tests place their buffers.
- **Lane counts:** each phase with 1, 2, 3 and 8 lanes, outputs compared byte
  for byte.
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
   the linker starts with one.
2. **Fixups from the encoder:** marked instructions, field offsets and
   addends. Done when they match GNU as's relocations on random streams, and
   the compiler-like mix encodes no slower than the offsets loop (0.72 ns per
   instruction).
3. **The object writer:** code, data, symbols and relocations from generated
   programs. Done when GNU ld, lld, mold and wild link its objects into
   programs that run, `readelf` agrees, and `benchmark-object-writer` reports
   per-object costs against the file floor.
4. **The direct path without libc:** a static executable whose `_start` makes
   system calls. Done when the tests' programs run.
5. **The runtime snapshot and the dynamic PIE:** binding, GOT, RELR, versions,
   the snapshot's validation and rebuild. Done when generated programs that
   call libc run, match their objects linked by lld, and `benchmark-link`
   meets the targets in "Summary".

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
- the performance governor;
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
