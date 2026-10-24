# The object writer and the linker

This is the design of the two components that turn the compiler's encoded
machine code and data into files:

- **The object writer:** ET_REL objects, for `-c` and for other linkers.
- **The linker:** executables written straight from the compiler, and links of
  other compilers' objects, archives and shared libraries.

The lane base layer, the encoder's fixups, the representation, the object
writer, the direct path's executables (static ones without libc and with it,
and dynamic PIEs against glibc) and the import, binding and snapshot of the C
runtime they need are implemented (`src/buster/compiler/lane.c`, `x86_64_encoder.c`,
`link.c`, `object_writer.c`, `import.c`, `runtime.c`, `bind.c`,
`executable.c`, `dynamic.c` and `snapshot.c`, with generated test programs in
`link_program.c`), and `benchmark-link` measures the whole link against the
linkers ("The whole link"); the rest of the linker isn't yet. The document
records the measurements on the Ryzen 7 9700X that the design rests on, the
design itself, and the order to build it in, as `docs/x86-64-encoder.md` does
for the encoder.

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
  a *runtime snapshot*, a cache file mapped and checked on each link with one
  `stat` per file it was made from: 9.2 µs on the 9700X, against 152 µs for
  importing the runtime.
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

The direct path meets them. `benchmark-link` links generated programs that
call libc of these sizes, the largest of synth's shape (its 102,400 functions
in 512 units, 44 MB of output), by the direct path, from the snapshot's load
to the closed file, and from the object writer's objects of the same
programs by each linker, as the appendix measured them ("The whole link"):

| Program (output) | Direct path, 1 lane | mold, 1 thread / 15 threads | wild, 1 / 15 | Factor (vs wild at 15 threads) |
| --- | ---: | ---: | ---: | ---: |
| 1 worker (7.2 KB) | 14.6 µs | 2.46 / 5.57 ms | 1.35 / 2.58 ms | 176× |
| 2,000 workers (513 KB) | 75.0 µs | 3.29 / 6.67 ms | 2.50 / 2.91 ms | 39× |
| 102,400 workers (44 MB) | 6.65 ms | 99.4 / 36.9 ms | 99.9 / 25.2 ms | 3.8× |

On these programs mold, wild and lld take what they took on hello world, tcc
and synth, within 5% with their own threads. The writer's phases are in "The
first executables", "The dynamic PIE" and "The unwind tables". Linked with
`-static`, the same programs take 0.29, 0.36 and 7.7 ms, against mold's 8.7,
10.0 and 41.7 ms with its threads and wild's 4.0 and 25.9 ms at the two larger
sizes ("The whole link").

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

The full table is in the appendix, with the exact commands. `benchmark-link`
now links generated programs of these sizes and of synth's shape the same way,
on which the linkers take within 5% of these times ("The whole link").

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
pinned to CPU 2 (medians of 5–15 runs). `benchmark-floors` measures all of it
again. A run on 2026-10-21 (kernel 6.18.50) reproduced the file writes from
4 MB up within 11%, but `O_TRUNC` on btrfs (1.5 ms for 4 MB rather than
10.4), the smaller ones within a third, and the small calls within 13%, but
touching all of `libc.so.6` (5.9 µs rather than 8.2). Writing from several
threads on tmpfs reached 12–24% fewer GB/s, but `pwrite` in place, and tearing
down base pages took twice as long: 24 ms and 104 ms for 1 GB and 4 GB (1.2 ms
and 4.6 ms with huge pages).

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

- a dozen `stat` calls, about 2 µs, to check the runtime snapshot (19 as
  built, 4.1 µs: "The runtime snapshot");
- reading the snapshot, a few microseconds (mapping and hashing its 192 KB,
  4 µs as built);
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

**Object writer.** Building an object costs about as much as writing its file,
measured: 14 µs for an object like one of synth's translation units (151 KB),
against 8 µs to overwrite its file in place on tmpfs and 35 µs to write a new
one on btrfs, so an object costs 1.5–3× its file in all ("The object writer
(`-c`)"). Objects aren't on the direct path's way, so their speed matters for
`-c` builds and for other linkers.

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
- **The hash** is `link_hash`: 8 bytes at a time, each word mixed in with an
  XOR, a 64-bit multiplication and a shift that folds the product's high half
  down, then SplitMix64's finalizer. Each step is an AVX-512 instruction on
  eight 64-bit lanes (`vpxorq`, `vpmullq`, `vpsrlq`), so `link_hash_names`
  hashes eight names at once: each name's first 64 bytes in one masked load
  (a name may end at a page's end), the rows transposed so that a vector holds
  word j of the eight names, and the words mixed in lane by lane while a
  lane's name has them. Names past 64 bytes, and those after the last whole
  batch, go to `link_hash`, which is also the tests' oracle. From the runtime
  snapshot on it is part of the format, which test-link pins with known
  values. On libc's 2,912 names, 300,000 generated ones and 96,000 random
  ones, its low 10, 12 and 16 bits and bits 26 and 52 up are as uniform as
  random ones (χ² per degree of freedom 0.94 to 1.07).
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
literals: `.rodata.str1.1` in an object), `_EH_FRAME` (the unwind tables'
CIEs and FDEs, as an object's `.eh_frame` holds them), `_TEXT`, `_INIT` and
`_FINI` (the C runtime's `.init` and `.fini`: fragments of one function each,
from `crti.o`'s prologue to `crtn.o`'s epilogue), `_TDATA`, `_TBSS`,
`_INIT_ARRAY`, `_FINI_ARRAY`, `_DATA_RELRO`, `_DATA` and `_BSS`. `_TBSS` and
`_BSS` are zero-fill. The atoms of `_EH_FRAME`, `_INIT`, `_FINI`,
`_INIT_ARRAY` and `_FINI_ARRAY` keep the order of their ids, which is their
inputs' link order, since that order is their meaning.

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
| `CALL_GOT` | `call` or `jmp [rip + slot]` (`ff 15`, `ff 25`) | `R_X86_64_GOTPCRELX` | G + A − P, or when S is the image's and not an IFUNC, S + A − P with `call` rewritten to `addr32 call` and `jmp` to `jmp` and a `nop` |
| `LOAD_GOT` | `mov r64, [rip + slot]` | `R_X86_64_REX_GOTPCRELX`: loading a pointer has REX.W | G + A − P, or when S is the image's and not an IFUNC, S + A − P with `mov` rewritten to `lea` |
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
`GOTPCREL`, which linkers leave alone. GNU as marks `test`, binops and a `mov`
without REX.W with `GOTPCRELX` and `REX_GOTPCRELX` too, so the import reads
the bytes before each such relocation's field and gives it `CALL_GOT` or
`LOAD_GOT` only on those instructions with the addend -4, and `GOTPCREL`
otherwise: 379 of the static runtime's on the 9700X, `libgcc.a`'s adds of
slots.

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
  `libc_nonshared.a` and `ld-linux-x86-64.so.2`;
- the loader, `ld-linux-x86-64.so.2`, which the script names as needed: 38
  exports, among them `__tls_get_addr` (at `GLIBC_2.3`), `_r_debug` and
  `__libc_stack_end`. A link takes them only for a name that `libc.so.6`
  doesn't export, and GNU ld and lld then add the loader to `DT_NEEDED`.

mold and wild open, map and parse all of this on every link. The compiler does
it once and keeps the result in a snapshot under `$XDG_CACHE_HOME/buster/`.

**Contents:**

- **The crt objects and `libc_nonshared.a`,** imported and laid out together.
  Their internal references are resolved. What remains are fixups against the
  program and the dynamic sections: `main`, `__libc_start_main@GLIBC_2.34`
  through the GOT, `_init`/`_fini`, `__dso_handle`, the `.init_array` entries
  (`frame_dummy`), and weak undefined references (`__gmon_start__`, the
  `_ITM_*` clone table functions) that resolve to 0.
- **libc.so.6's and the loader's exports:** each name's default version and
  type, the loader's version indices numbered after libc.so.6's, so that a
  symbol's version names one library's; and the runtime's *global names*:
  each distinct name of the runtime objects' global symbols, of libc.so.6's
  exports and undefined names and of the loader's exports, with what binding
  needs of it (its definition, its export, libc.so.6's before the loader's,
  whether libc.so.6 refers to it, which decides the program's exports, and
  whether the linker defines it),
  indexed by the front end's name hash in buckets filled by a counting sort,
  with a blocked Bloom filter ("Step 5's phases, data-parallel"). The other
  shared libraries in the link will get the same.
- **For `-static`, libc.a's 2,231 members:** each member's atoms and fixups,
  already converted; a table from symbol to defining member; and each member's
  undefined names. libgcc.a, libgcc_eh.a and libatomic.a get the same ("A
  static executable's runtime, as built", below).
- **The search results** that led to each file: every path probed, in the
  search's order, found or absent, with its identity.

**Validation.** The key is the identity of the compiler's executable and of
each file: `st_dev`, `st_ino`, `st_size`, `st_mtim` and `st_ctim`. A package
upgrade replaces the inode and changes all of these. `st_ctim` changes on any
write, and no system call sets it. The compiler's own identity stands for its
version and for the snapshot's layout: a rebuild of the compiler changes it,
so a snapshot is only ever read by the executable that wrote it, with the same
struct layouts. The absent candidates are checked so that a library newly
placed earlier in the search path is noticed. The options that change the
runtime get a snapshot of their own: `-static` has one; `-pie`, `-nostdlib`
and the search paths will.

**As built** (`snapshot.c`: `runtime_open` and `runtime_open_static`, with
`test-snapshot` and `benchmark-snapshot`). The snapshots are
`runtime.snapshot` (a dynamic PIE's runtime) and `runtime-static.snapshot` (a
static executable's) in `$XDG_CACHE_HOME/buster`, or `$HOME/.cache/buster`
without it, made private (mode 0700) when it is missing. Each holds its
kind, which the loader checks: the other kind's file at its name is stale.

- **The file** is the runtime's records as the compiler lays them out in
  memory, each region at a multiple of 64 bytes, in two parts:
  - **the directory,** what every link reads: a header (the counts, the paths
    and each region's place), then from the second page the probes,
    `libc.so.6`'s versions, the members, the strings, the pieces (the crt
    objects and the members, each a range of every table's records), each
    piece's hash, the global names with their index and filter, the exports,
    and the pieces' references and COMDAT groups;
  - **the pieces' records,** of which a link reads those of the pieces it
    joins: the link's columns, the bindings and the symbols' global names,
    then the data, the names run and each piece's atoms' bytes.

  The dynamic runtime's is 198 KB on the 9700X: 2,685 global names with their
  index and filter, and `libc.so.6`'s 2,630 exports and the loader's 38. The
  "few tens of kilobytes" this
  section first expected were the exports' columns alone. The static
  runtime's is 8.6 MB, 5 MB of it the members' bytes.
- **Loading** maps it privately. The runtime's columns point into the mapping
  and are used where they are, which is why the global names hold their bytes
  as offsets into one string pool rather than as pointers. The probes and
  `libc.so.6`'s versions hold pointers stored as offsets plus 1 (0 for none),
  rebased where they are, which copies their page alone. The members' names
  and the link's names' and atoms' bytes hold such offsets too, but stay as
  they are: the runtime's deltas make them pointers where binding reads them
  (`runtime_name_bytes`, `runtime_atom_bytes` and `runtime_member_name`), so
  loading writes none of those columns' pages.
- **It is trusted a piece at a time:**
  - the directory, whole, when it loads: its magic, version and size must be
    right, its 128-bit hash (the build ID's chunk hash, over the file up to
    the pieces' records with the hash's field as zeros) must be its bytes',
    the executable that wrote it must be this one, its kind the one asked for,
    and every probe as it was;
  - each piece when binding first reads it (`runtime_piece_current`, in
    `bind_join`, and in `bind_begin` for `crtend.o`'s and `crtn.o`'s, whose
    symbols binding reads before they join): its records must hash as the
    directory says.

  A link thus reads only what this executable wrote, and checks only what it
  reads. A torn, truncated or corrupt file is rebuilt rather than trusted, so
  the writer needs no `fsync`:
  - a damaged directory makes the file invalid;
  - a damaged piece fails the link that joins it (`BIND_ERROR_SNAPSHOT`),
    whose caller discards the file (`snapshot_discard`) and opens the runtime
    again.

  The piece hash takes what binding reads, 26 slices of the piece's columns
  and data, each a 64-byte block at a time, through masked loads, into eight
  accumulators that rotate after every block. Each block is two AES rounds,
  as the chunk hash takes them. The chunk hash's stream with a carry would
  copy every slice's tail. The columns nothing reads of a loaded runtime are
  in no piece's hash: the atoms' addresses, the symbols' GOT slots, dynamic
  indices, versions and references (the linker's, which the runtime leaves
  0), and the bindings, which only the import uses.
- **A stale or missing snapshot** is rebuilt by the import (the same code as a
  link without a snapshot), written into a new file under a random name beside
  it and renamed into place. A process that has the old one mapped keeps it
  whole, and two compilers rebuilding it at once write identical files, the
  last rename winning, so no lock is needed. A snapshot that can't be written
  (a directory at its path, a cache directory that can't be made) doesn't fail
  the link, which goes on with the imported runtime.

On the 9700X (`benchmark-snapshot`, one CPU, medians of 41 runs, on tmpfs; on
btrfs, the cache directory's file system there, within 0.4 µs but for the
write), in µs, with a program of one worker bound against each. The static
PIE's runtime (`runtime_open_static_pie`, in `runtime-static-pie.snapshot`)
was measured later, at 5cd1e31, which added it, in a run whose dynamic and
static rows came within 3% of those here as rounded, but for the dynamic
runtime's binding against one loaded afresh (3.3) and its pieces' checks
(0.3):

| Step | Dynamic | Static | Static PIE |
| --- | ---: | ---: | ---: |
| `runtime_import` (`_static`, `_static_pie`) and `runtime_release`, without a snapshot | 147 | 1,894 | 1,892 |
| `runtime_open` from the snapshot | 9.6 | 22.8 | 22.9 |
| — the compiler's identity: `stat` of `/proc/self/exe` | 0.5 | 0.6 | 0.6 |
| — `open`, `fstat`, `mmap` and `close` | 1.0 | 1.2 | 1.1 |
| — the header and the directory's hash | 3.3 | 15.5 | 15.8 |
| — the columns pointed into the mapping, the probes and versions rebased | 0.6 | 0.8 | 0.7 |
| — the probes' `stat` (19, 20 and 20) | 4.1 | 4.6 | 4.6 |
| `runtime_release` of a loaded runtime (`munmap`) | 1.9 | 6.3 | 6.3 |
| `snapshot_write` (tmpfs) | 48 | 1,543 | 1,533 |
| the file read with `pread` into new memory instead | 22 | 241 | 241 |
| mapped without the hash, then 12 lookups | 7.8 | 13.0 | 13.0 |
| mapped and hashed whole, as format 3 checked it | 4.2 | 155 | 156 |
| binding, against the imported runtime | 2.7 | 138 | 139 |
| binding, against one loaded afresh, as a link binds | 2.9 | 221 | 222 |
| binding, against one loaded and warm | 3.0 | 165 | 165 |
| — the checks of the pieces it read (5, 447 and 448), warm | 0.2 | 41 | 40 |

- **Mapped, not read.** `pread` puts the file into new anonymous pages, whose
  first touches make it cost 22 µs for the dynamic runtime's 197 KB; mapping
  the page cache's own pages costs 1 µs, and their first reads under the hash
  2 µs more.
- **The static runtime is checked by the piece.** Format 3 hashed the whole
  file before trusting any of it, 155 µs of the static runtime's 209 µs load,
  where a hello world's link reads 447 of its 2,622 pieces. Format 4 hashes
  the directory, about 0.9 MB, in 15.5 µs, and binding checks the pieces it
  joins, 41 µs warm. The pages the hash used to fault in, binding faults in
  now, about 40 µs more against a fresh mapping than a warm one.
  - Load, binding of one worker and release, in a harness that times all
    three on the 9700X: 385 µs warm and 678 cold (L3 evicted) with format 3;
    284 and 449 with format 4.
  - Hashing all 32 of the piece's columns took a fifth longer (50 µs), and
    faulted in pages binding never reads.
  - One AES round a block instead of two saves 1.5 µs; two plain sums a
    block, 4 µs. Most of the check's cost is per slice, not per block.
  - `MAP_POPULATE` makes the load 750 µs: a private writable mapping is
    populated by write faults, which copy every page.
- **The probes are the largest part of a dynamic link's load:** 19 `stat`
  calls at 0.215 µs. Eight are absent candidates in directories the 9700X
  doesn't have (Debian's multiarch ones). A probe of each directory instead
  would leave 13, about 2.8 µs, since a directory's times change when a file
  is added to it; it is left for later. `statx` costs 0.24 µs a call, more
  than `stat`.
- **Binding the dynamic runtime is as fast loaded as imported:** the directory
  hash has already read what it reads, and its pieces are a few crt objects.
- **A static PIE's runtime costs what a static executable's does.** It is the
  same archives with `rcrt1.o`, `crtbeginS.o` and `crtendS.o` for `crt1.o`,
  `crtbeginT.o` and `crtend.o`: 23,986 symbols to 23,992, a snapshot of
  8,670,305 bytes to 8,670,737. Its import end to end (`benchmark-import`)
  takes 1.82 ms, against `runtime_import_static`'s 1.85 in the same run. A
  program bound against it joins one member more, `libc.a`'s
  `dl-reloc-static-pie.o`, which relocates the executable at startup
  (`rcrt1.o` doesn't define `_dl_relocate_static_pie`, as `crt1.o` does), so
  binding checks one piece more.

**Finding and importing the files** (`runtime.c`, with the readers of
`import.c`). GCC's installation is the highest version under
`/usr/lib/gcc/<triple>` or `/usr/lib64/gcc/<triple>` that has `crtbeginS.o`,
for the triples distributions use (`x86_64-pc-linux-gnu` on Arch,
`x86_64-linux-gnu` on Debian and Ubuntu). glibc's files are searched in the
multiarch directories, then `/usr/lib64` and `/lib64`, then `/usr/lib` and
`/lib`, as GCC's driver searches them; `libc.so`'s script names the rest by
absolute path. Every path probed, found or not, is recorded with what the
validation compares (`runtime_probes_current` compares them again): among
them the directory the version was chosen from, whose times change when a
version's directory comes or goes, and each version's `crtbeginS.o`, found or
not, so that one installed into a version's existing directory shows too. The
import reads each file once into an arena that the atoms point into:

- the crt objects become units 0 to 4, in the driver's order, and each member
  of `libc_nonshared.a` a unit after them. An object's allocated sections
  become atoms of the class lld gives them; `.sframe` (which GCC 16's
  `Scrt1.o` has, and lld 23 copies into the output), notes and `.comment` are
  dropped. `Scrt1.o`'s `.note.ABI-tag` is kept aside for the executable's
  `PT_NOTE`;
- `libc.so.6` becomes a table of its exports, each at its default version
  (`name@@VERSION`, or unversioned), with its type and size, an IFUNC as a
  function; its version definitions with their hashes; and its undefined
  names;
- each name the crt objects and members refer to without defining binds to
  another of them (`__TMC_END__` to `crtendS.o`), to the linker for a name it
  defines (`_GLOBAL_OFFSET_TABLE_`, which GNU as names in an object that uses
  the GOT), to a `libc.so.6` export (`__libc_start_main`, `__cxa_finalize`),
  to nothing for a weak reference that neither defines (`__gmon_start__`, the
  `_ITM_` functions), or to the program (`main`).

The readers bound every offset, size, count and index they read by the file
or the table it indexes. A malformed file gets an error that names the
section, symbol or relocation and the byte offset, and the readers are run on
every prefix of their test inputs and on every byte changed, ending at a
`PROT_NONE` page, to show that they read nothing past the end.

**A static executable's runtime, as built** (`runtime_import_static`, the
first part of step 6). GCC's driver links `cc -static` as `crt1.o crti.o
crtbeginT.o` before the program's objects and `crtend.o crtn.o` after them,
with `--start-group -lgcc -lgcc_eh -latomic_asneeded -lc --end-group` between
(GCC 16 installs `libatomic_asneeded.a` as a link to `libatomic.a`; GCC 13
links neither). `-static-pie` takes `rcrt1.o`, `crtbeginS.o` and
`crtendS.o` instead, with `-static -pie --no-dynamic-linker -z text`: the
static PIE's runtime (`runtime_import_static_pie`, step 6's fifth part) is
the same archives with those crt objects, in a snapshot of its own. The
search finds the crt objects as for a dynamic PIE, and each archive where
`-L` finds it: GCC's installation, then the library directories. The import
makes the crt objects units 0 to 4 and each archive's members, in the
group's order, the units after them. On the 9700X (GCC 16.2, glibc 2.44):
2,622 members, 298 of `libgcc.a`'s, 6 of `libgcc_eh.a`'s, 87 of
`libatomic.a`'s and 2,231 of `libc.a`'s, with 23,992 symbols, 40,954 fixups,
5,800 global names, 10,621 references and 79 COMDAT groups. What the static
runtime has that the dynamic one doesn't:

- **COMDAT groups:** each member that uses C cleanups keeps
  `DW.ref.__gcc_personality_v0`, the personality routine's address that its
  CIE points to, in a group of its own, and each that has SystemTap probes
  `.stapsdt.base`. The import reads each object's groups (only `GRP_COMDAT`
  ones; a section with `SHF_GROUP` in no group or in two is refused), and the
  runtime numbers their signatures among its distinct ones (2), so that
  binding keeps the first group of each in link order and discards the rest.
- **IFUNCs:** `libc.a`'s string and memory functions (100 names on the
  9700X), whose resolvers the executable's IRELATIVE relocations call at
  startup.
- **Absolute symbols:** `_nl_current_LC_CTYPE_used` and the like, which
  glibc's locale code reads through the GOT to see which categories are
  linked in.
- **The general-dynamic and local-dynamic TLS kinds** (`R_X86_64_TLSGD`,
  `TLSLD` and `DTPOFF32`): `libgcc.a`'s decimal float and split-stack
  members are built PIC (243 general-dynamic accesses in 24 members on the
  9700X), and so are GCC 16's heap trampolines in `libgcc_eh.a`
  (local-dynamic). They call `__tls_get_addr`, which nothing in a static
  link defines: GNU ld and lld relax every such sequence to local-exec,
  which consumes the call.
- **Prioritized init and fini arrays** (`.init_array.NNNNN`), which the
  linkers place before the plain ones, by priority: two of `libgcc.a`'s.
- **The linker's names:** besides the arrays' bounds and the GOT's symbol,
  the ELF header's address (`__ehdr_start`, through which `libc.a`'s startup
  finds the program headers), `__executable_start`, the preinit array's
  bounds, the IRELATIVE relocations' (`__rela_iplt_start` and
  `__rela_iplt_end`), and the ends of the text, the data and the image.

The runtime refers, without defining them, to `main`, to `__tls_get_addr`
(from those 25 PIC members on the 9700X) and to `_DYNAMIC`, from
`dl-reloc-static-pie.o`, which a link without `-static-pie` never loads:
`crt1.o` defines `_dl_relocate_static_pie`, the one name it would be loaded
for. `rcrt1.o` doesn't, so a static PIE's closure loads it, and the linker
defines `_DYNAMIC` for the PIEs. A static PIE's linker doesn't define the
IRELATIVE relocations' bounds, which GNU ld and lld leave undefined in a PIE:
`libc.a`'s references to them are weak, and its IRELATIVE relocations are in
`.rela.dyn`. For binding's closure, the global names say which member defines each
name, and each member's strong references to names are listed; on both
systems every name but two is defined by at most one member, and those two
(`DW.ref.__gcc_personality_v0` in 52 members on the 9700X and
`_.stapsdt.base` in 27) are weak definitions in COMDAT groups. The snapshot
holds the static runtime as it holds the dynamic one, in a file of its own
(8.6 MB on the 9700X), which `runtime_open_static` opens ("As built", above).

**Static links, as built** (`bind.c`, the second part of step 6). Binding
against the static runtime is binding against the dynamic one with the
archives' members in place of `libc.so.6`. `bind_begin` joins `crt1.o`,
`crti.o` and `crtbeginT.o` after the program's symbols, as for a PIE. Then
`bind_end`, after the program's atoms, finds the members the link needs (the
closure), joins them in archive order, binds every name and joins `crtend.o`
and `crtn.o`. No name binds to a shared library.

- **The closure** is GNU ld's for the driver's group. A strong reference to a
  name that nothing in the link defines (the program, a crt object before it,
  a member that joined, or the linker) loads the member that defines it. A
  weak reference loads nothing, and binds to a definition loaded for another
  reason or to nothing: a program that tests a weak `puts` sees it null
  unless something else loads `puts`'s member. A weak definition satisfies
  references, so a program's weak `__libc_start_main` keeps its member out.
  Only the two COMDAT names have more than one member defining them, so the
  members don't depend on the order the walk visits them in. They are the
  members GNU ld 2.47 and lld 23.1 load for the same objects, which
  `test-bind --cross-check` reads from their traces.
- **Two paths.** The exact path is a queue of members, each reference tested
  against the link's symbols and the members needed so far, one at a time.
  The vector path works in rounds over a bitset of the members. A round takes
  each member of its frontier and the member each of its references loads,
  16 references at a time (the first 16 of a member's without a branch:
  most members have fewer). That member comes from a column made at import
  (`Runtime.reference_members`), a load rather than a gather. The members
  loaded are compressed onto the round's list, which is marked after the
  round, a byte per member; the marks, 64 at a time, less the members needed
  already, are the next frontier's word. A name the program defines that a
  member defines too must not load the member: the vector path compares each
  batch's references with up to 16 such names in registers, and with more
  leaves the closure to the exact path's. The generated programs define
  none.
- **COMDAT groups:** the first group of each signature in link order joins. A
  later one's atoms don't, its definitions join as references of their names,
  and a fixup of an atom that joins to one of its local symbols is an error
  (`BIND_ERROR_DISCARDED`), as GNU ld's reference to a discarded section is.
  A member with a group to discard joins a record at a time on either path.
- **What nothing defines:** `__tls_get_addr`, which the PIC members' general-
  and local-dynamic accesses call, stays undefined for the writer, whose
  relaxation to local-exec consumes the call. Any other strong reference to a
  name nothing defines is an error.

Measured on the 9700X (`benchmark-bind`, one CPU, medians of 21 runs), for
the generated programs that call libc: each joins 442 of the 2,622 members,
with 2,248 atoms and their 707 KB of bytes, 5,844 symbols, 11,942 fixups and
5,270 names. The closure takes 12 rounds over the members' 2,459 references,
384 batches of them.

| Workers | `bind_begin`, vector / exact | Closure, vector / exact | `bind_end`, vector / exact |
| --- | ---: | ---: | ---: |
| 1 | 1.02 / 0.92 µs | 1.43 µs, 3.24 ns per member / 2.44 µs, 5.51 ns | 74.5 / 121.5 µs |
| 2,000 | 2.52 / 5.50 µs | 1.63 / 2.44 µs | 74.1 / 117.6 µs |
| 102,400 | 92.5 / 810.5 µs | 1.81 / 3.61 µs | 97.9 / 158.8 µs |

The vector closure first marked each batch's members as soon as it had
compressed them, and was slower than the exact path (3.15 µs against 2.42):
the byte stores read the members back from the batch's masked store, which
doesn't forward to a load, so every batch waited for its store to commit.
Marking the round's list after the round, when its stores have left the
store queue, cut it to 1.43 µs.

`bind_begin` is the dynamic runtime's, with `crt1.o`, `crti.o` and
`crtbeginT.o` for `Scrt1.o`, `crti.o` and `crtbeginS.o`. `bind_end` is the
members' joins, and at 102,400 workers the scan of the program's symbols for
the names nothing defines too (24 µs, as for the dynamic runtime). Timed with
the TSC around each join, the 419 members that join as blocks take about
61 µs, 145 ns each, and the 23 that discard a group about 17 µs, a record at
a time; the closure, binding the names and the tail objects take the rest. A
block join's cost is per record more than per member, and no phase of it
stands out: copying the atoms' bytes is the largest, about a fifth (the
writer patches atoms in place, so the bytes must be the link's own). Copying
a member's dozen small columns by masked 64-byte stores rather than calls of
`memcpy` changed nothing. Per record, the 23 members' joins cost 15.5 TSC
ticks against a block join's 10.8, so joining them as blocks would save
about 5 µs: they stay the rare case, left to the exact join.

Against a static PIE's runtime (`runtime_import_static_pie`, with `rcrt1.o`,
`crti.o` and `crtbeginS.o` before the program and `crtendS.o` and `crtn.o`
after it), the programs join 443 members: the static executable's 442 and
`dl-reloc-static-pie.o`, the member lld's `--why-extract` adds for the same
objects linked `-static-pie`. Binding costs what it costs against the static
runtime. In one run of `benchmark-bind` at 5cd1e31, vector / exact:

| Workers | Runtime | `bind_begin` | Closure | `bind_end` |
| --- | --- | ---: | ---: | ---: |
| 1 | static | 0.80 / 0.95 µs | 1.37 / 2.46 µs | 73.4 / 123.5 µs |
| 1 | static PIE | 0.79 / 0.86 µs | 1.37 / 2.48 µs | 74.9 / 123.0 µs |
| 2,000 | static | 2.56 / 5.57 µs | 1.56 / 2.41 µs | 74.1 / 121.7 µs |
| 2,000 | static PIE | 2.54 / 5.50 µs | 1.46 / 2.44 µs | 74.5 / 123.0 µs |
| 102,400 | static | 92.4 / 818 µs | 1.87 / 3.62 µs | 97.7 / 142.2 µs |
| 102,400 | static PIE | 92.6 / 818 µs | 1.76 / 3.60 µs | 99.6 / 142.0 µs |

**Shared libraries, as built so far** (`runtime.c` and `bind.c`, step 6's
fifth part). GCC's driver links `-shared` with the dynamic PIE's crt objects
but `Scrt1.o` (`crti.o` and `crtbeginS.o` before the objects, `crtendS.o` and
`crtn.o` after them), around the same libraries. So a shared library's runtime
(`runtime_import_shared`) is the dynamic one without a start object: its unit
and piece are there, empty, so that each crt object keeps its unit, and
nothing refers to `main`. Its snapshot (`runtime-shared.snapshot`) is 196 KB
on the 9700X, with 16 probes and 2,680 global names; importing it takes
153 µs, and opening it from the snapshot 8.9 µs. Binding against it is binding
against the dynamic runtime with two of GNU ld's and lld's `-shared` rules:

- **A name nothing defines** stays undefined when a strong reference of
  default visibility names it: a dynamic symbol for the loader to bind in the
  libraries the program loads (`-z undefs`, which `-shared` defaults to). A
  hidden reference to it is an error, as for a PIE.
- **Every definition with default or protected visibility is an export**
  (`bind_shared_exports`), not only those of names a library defines or
  `libc.so.6` refers to. The crt objects' and `libc_nonshared.a`'s global
  names are all hidden, so the exports are the program's. The vector path
  takes 16 symbols at a time, a mask of their flags', atoms' and sources'
  columns, compressed in a register and stored with a mask of its count; its
  exact twin takes a symbol at a time.

`test-bind --cross-check` compiles three C libraries with `-fPIC` and compares
binding with lld's `-shared` link of each: the same `.dynsym`, with the
imports at their versions (`__tls_get_addr@GLIBC_2.3` from the loader), the
undefined and weak references, and the exports.

Measured on the 9700X (`benchmark-bind`, one CPU, medians of 21 runs, vector
/ exact), binding a program as a shared library costs what binding it as a
dynamic PIE does but for the exports:

| Workers | Runtime | `bind_begin` | `bind_end` |
| --- | --- | ---: | ---: |
| 1 | dynamic PIE | 0.45 / 0.62 µs | 0.39 / 0.32 µs |
| 1 | shared library | 0.26 / 0.35 µs | 0.26 / 0.22 µs |
| 2,000 | dynamic PIE | 2.05 / 4.93 µs | 0.75 / 0.64 µs |
| 2,000 | shared library | 2.02 / 4.84 µs | 1.05 / 1.54 µs |
| 102,400 | dynamic PIE | 90.4 / 756 µs | 25.2 / 20.8 µs |
| 102,400 | shared library | 90.1 / 755 µs | 41.9 / 63.3 µs |

At 102,400 workers the exports pass over the link's 112,677 symbols, 106,260
of them exports, is about 17 µs of `bind_end`'s 42: 13 cycles per 16 symbols,
which read three columns and write the 425 KB of exports. With the export
count and the columns' pointers read from the structures in the loop, as
`-fno-strict-aliasing` has clang reload them after each store, it took 43 µs
and the exact path 75; with them in locals, 42 and 63. Storing the compress
straight to memory (`vpcompressd` with a memory destination) measured the same
as compressing in a register and storing with a mask: 43.2 to 43.5 µs against
42.8 to 42.9, both with the reloads.

**What the snapshot buys.** Synthetic sections and patching for the runtime
are a few dozen records. `__libc_start_main`, `__cxa_finalize` and the
program's imports each get a GOT slot and a `GLOB_DAT`. `__dso_handle` and the
two init/fini entries become relative relocations. No ELF is parsed on the
link's path.

#### Step 5's phases, data-parallel

Every phase of step 5 is designed as `c_lexer.c` and the encoder are:
data-parallel, vectorized and branch-free first, with an exact scalar path for
what the vector path leaves to it, which is also the oracle the tests compare
the vector path with. A phase is never taken to be inherently sequential: the
question is which representation and algorithm vectorize. The tools are prefix
sums, compression and expansion in registers, permutes as table lookups,
sorting and segmented scans, and bitsets. Gathers and scatters are left out:
`vpgatherdd` (zmm) is 64 µops and about 12.6 cycles on Zen 5, `vpscatterdd`
88 and 17, so the joins are scalar loads, permutes or sorted streams.

What is done, in `import.c` and `runtime.c`, with each reader's vector path
against its exact path on the 9700X (`benchmark-import`, one CPU, medians of
21 runs):

| Input | Records | Vector | Exact | |
| --- | ---: | ---: | ---: | ---: |
| 64 objects of a generated program (one of synth's translation units each) | 231,604 | 0.49 ms, 2.11 ns per record | 5.10 ms, 22.0 ns | 10.4× |
| the same with function sections | 231,670 | 0.71 ms, 3.07 ns | 3.13 ms, 13.5 ns | 4.4× |
| one object of 33,000 functions in sections of their own | 430,617 | 1.06 ms, 2.45 ns | 6.17 ms, 14.3 ns | 5.8× |
| the crt objects and `libc_nonshared.a`'s members | 94 | 1.36 µs | 1.74 µs | 1.3× |
| a static executable's crt objects and archives' 2,622 members | 71,412 | 0.66 ms, 9.18 ns | 1.47 ms, 20.5 ns | 2.2× |
| `libc.so.6` (glibc 2.44) | 2,651 names | 11.0 µs, 4.2 ns per name | 41.9 µs, 15.8 ns | 3.8× |
| `libstdc++.so.6` | 6,345 names | 83 µs, 13.2 ns | 214 µs, 33.8 ns | 2.6× |
| `libLLVM.so.23.1` | 48,048 names | 1.47 ms, 30.6 ns | 1.98 ms, 41.2 ns | 1.35× |
| `libc.a` (2,231 members) | 4,880 index entries | 28 µs, 5.7 ns per entry | 111 µs, 22.7 ns | 4.0× |
| `libgcc.a` (298 members) | 699 index entries | 3.2 µs, 4.5 ns | 10.4 µs, 14.9 ns | 3.3× |
| `libc_nonshared.a` | 5 index entries | 0.12 µs | 0.16 µs | 1.3× |

The records are an object's symbol table entries and relocations, and a
library's exports and references. The small inputs are bound by what each
file costs whatever its size: the ELF header, the arrays pushed, the tables
set up. libLLVM's names are C++'s, mostly past 64 bytes, which take memchr and
the scalar hash.

- **Objects** (`import_object_vector`). Eight records at a time, one per
  64-bit lane, written straight into the link's columns past their counts:
  - *sections:* eight headers transposed 8×8 (`link_transpose_words`), so
    that a vector holds one field of eight sections. Bounds, alignments and
    the class chain of `import_section_class` are lane masks, and the 16 first
    bytes of each name are compared with the names the classes depend on
    (`.init`, `.fini`, `.eh_frame`, `.sframe`, `.data.rel.ro`) by a ternary
    logic op and a test per pattern. Then the atoms are compressed into the
    columns, and each section's atom id is expanded into its lane;
  - *symbols:* eight 24-byte entries in three vectors, transposed to their
    three words by six `vpermt2q`. Each symbol's atom, and the atom's class
    and size, are a scalar load per symbol and column, in a pass of their own
    between two vector passes. Then the classification of
    `import_object_symbols` and the columns, as for sections;
  - *names:* a masked 64-byte load per name gives its length (its first NUL).
    Eight at a time, a second masked load of each gives its hash
    (`link_hash_rows`, the same function as `link_hash`) and its copy, into
    one push from the name arena per batch. The lengths are a pass of their
    own because eight scalar stores read back as one vector can't be
    forwarded and stall the load;
  - *relocations:* eight entries in three vectors, the same way. Each type's
    kind and field width come from a 64-byte table in a register (`vpermb`).
    The order and overlap checks compare each field with the previous lane's
    end (`valignq`), and the side table's indices are an expand of the wide
    addends' ranks. `R_X86_64_NONE` entries are compressed away: a batch
    without them skips the compression, and one without a wide addend the side
    table. Each target is then a scalar load per fixup, in a tight pass.
    A section whose fields aren't in offset order is read again from a copy
    sorted by offset: a key per entry, its offset above its index, built eight
    at a time, then an LSD radix sort of the keys by as many of the offset's
    bytes as the largest offset has, which keeps equal offsets in the file's
    order, as the exact path's merge sort does. GNU as writes a relaxed
    branch's relocation after the section's others, so a tail call's `jmp`
    to another function leaves its `.rela.text` out of order: 261 of Ubuntu
    24.04's 2,481 members of the static runtime's archives and 221 of the
    9700X's 2,622, which the exact path took until the sort.

  Anything out of the ordinary sends the whole object to
  `import_object_scalar`: a malformed record, fields that overlap, two
  relocation sections for one section, a string table without its final NUL,
  or a table without room for every record. `import_object` then returns the
  link's counts and the arenas to where they were before running it. On the
  9700X the vector path takes every object above, and every crt object and
  archive member of GCC's and glibc's, a static executable's among them, on
  the 9700X and on Ubuntu 24.04.
- **Archives** (`import_archive_vector`). The symbol index lists the header
  offset of each member that defines a global symbol, in archive order. Those
  members' headers are read on their own, from the index, rather than by
  walking the chain of headers from the start. From each one, only the
  members up to the next indexed one are walked: the members without a global
  symbol, 122 of glibc 2.39's 2,211 in Ubuntu's `libc.a`, none of
  `libc_nonshared.a`'s. The index's offsets are byte-swapped by `vpshufb`,
  checked in order, and ranked: an expand of the lanes where the offset
  changes, then a prefix maximum by shifts of 1, 2 and 4 lanes. Each header's
  size is parsed in a vector: the digits right-aligned by a shuffle, then
  multiply-adds of pairs, fours and eights. The index's names are split at
  their NULs, found 64 bytes at a time. An archive without an index, or with a
  special member past the first ones, is the exact path's.
- **Shared libraries** (`import_library_vector`). `.dynsym` is read eight
  entries at a time, as `.symtab` is. Each export's version is looked up in a
  512-bit set of the defined versions' indices in a register: its word by
  `vpermq`, its bit by a variable shift. The exports' and references' columns
  are compressed into place, then their names' lengths come from a masked load
  each and their hashes eight at a time. `.dynamic`'s tags are compared eight
  entries at a time. The version definitions are a chain of entries that only
  `vd_next` places, 47 in glibc 2.44's: both paths read them with the exact
  code.
- **The exports' index** (`import_index_build`). One bucket per export,
  rounded up to a power of two, by the hash's top bits, filled by a counting
  sort: the buckets by a vector shift, the counts, an exclusive prefix sum in
  vectors (`import_exclusive_scan`: shifts of 1, 2, 4 and 8 lanes and a
  carry), then each export placed in export order, so that a name exported
  twice is found at its first entry. Split across lanes, the counts would be
  per lane and the prefix sum would combine them, as rule 4's exchange does.
  The open-addressing table it replaced is built by inserts whose probe
  chains depend on the inserts before them, so it can't be split. The
  benchmark keeps the table to compare:

  | Exports | Index: build | Table: build | Index: lookup | Table: lookup |
  | ---: | ---: | ---: | ---: | ---: |
  | 2,630 (`libc.so.6`) | 2.52 µs | 1.33 µs | 2.67 ns | 2.13 ns |
  | 6,139 (`libstdc++.so.6`) | 6.65 µs | 4.96 µs | 3.15 ns | 2.48 ns |
  | 47,626 (`libLLVM.so.23.1`) | 66.6 µs | 114.7 µs | 10.8 ns | 10.0 ns |

  At libc's size the table is faster to build and to look up in; the index
  wins its build from tens of thousands of exports, and it is the one a
  group of lanes can build. A lookup reads its bucket's bounds, then scans its
  entries, about one export: a dependent load more than the table's probe.
  The snapshot stores the index, so a link only looks names up: a few hundred
  to a few thousand of them, under 2 µs either way.
- **The runtime's binding** (`runtime_bind`). The runtime's definitions are
  found eight symbols at a time (a compress of the ids whose binding isn't
  local, with an atom or absolute, and with a name) and indexed the same way.
  Each reference is then looked up, rather than every reference scanning
  every symbol.
- **Linker scripts** (`import_script`) have no vector path: `libc.so`'s is
  255 bytes, read in 0.15 µs once per rebuild of the snapshot. A vector lexer
  would classify 64 bytes at a time into word, space and punctuation masks,
  as `c_lexer.c` does, with comments left to the exact one.
- **The whole import** (`runtime_import`: the search's 19 probes, each file
  read, imported and bound) takes 96 µs, against about 13 µs for the readers
  alone. The rest is the probes and reading the files, above all
  `libc.so.6`'s 2.2 MB, of which the import reads only the dynamic sections.
  Importing the loader too added about 7 µs: `benchmark-snapshot`'s
  `runtime_import` and `runtime_release` take 152 µs on the 9700X, against 145
  without it. The snapshot saves all of it on a link. Reading only those
  sections, with `pread` after the section table, is what is left for the
  snapshot's rebuild. A static executable's runtime (`runtime_import_static`)
  takes 1.75 ms: the search's 20 probes 20 µs, reading its 10.6 MB of files
  0.37 ms, the archives' indices and members 80 µs, sizing the link by each
  object's headers 0.15 ms, the 2,627 objects 0.71 ms, binding the runtime
  0.14 ms, its 5,800 global names 0.28 ms, and the members' references and
  groups 18 µs (medians of 21 runs of each phase, one CPU). Each import
  starts from new arenas, whose first touches are in these figures.

Each vector path is tested against its exact path on every input the tests
have: every unit's object of test-link's programs, the objects, archives and
libraries made by hand, every malformed one, every page-edge input (each
prefix and each changed byte, ending at a `PROT_NONE` page), and this
system's runtime. The vector path may take an input only when the exact path
reads it without an error, and must then make the same records, column by
column. The import of an input must give the exact path's result and records
either way. The tests also check that each well-formed input takes the
vector path.

Binding, the second part (`bind.c`), joins the runtime to the program's link
and binds every name before any layout:

- **Order by construction.** The classes whose atoms keep their id order
  (`.init`'s and `.fini`'s fragments, `.eh_frame` and the terminator that
  `crtendS.o` ends it with, the init and fini arrays) must come out in GCC's
  link order. So the runtime's objects join around the program's atoms rather
  than being sorted later: `bind_begin` joins `Scrt1.o`, `crti.o` and
  `crtbeginS.o` after the program's symbols and before its atoms, and
  `bind_end` the members of `libc_nonshared.a` the link needs, then
  `crtendS.o` and `crtn.o`, after them. The test programs leave room for this
  (`link_program_generate_begin`, `_atoms` and `_end`).
- **One symbol per global name.** A global or weak symbol of the runtime's
  takes the link's symbol of its name, if there is one. So `bind_begin` first
  matches every global symbol of the program against the runtime's global
  names (`RuntimeNames`: each name of the runtime's global symbols, of
  `libc.so.6`'s exports and of its references, and of those the linker
  defines, once, built by `runtime_import`, indexed as the exports are). The
  match is the one part
  that grows with the program; the rest grows with the runtime, or with the
  names the program leaves undefined. Most of the program's names aren't the
  runtime's, so the match is mostly misses, and a miss mustn't touch the
  index: its buckets and hashes are 48 KB, which a large program's columns
  stream out of L1. So the names also have a blocked Bloom filter, a word per
  four names (8 KB for libc's), each name setting two bits of the word its
  hash's top bits choose. Eight symbols at a time, their names' hashes are
  gathered by name id, their filter words by the hashes' top bits, and a lane
  mask of both bits set sends about one miss in a hundred, and every hit, to
  the index. Gathers pay here where the patch kernel's didn't: the alternative
  is two dependent scalar loads a symbol, and the filter's words stay in L1.
- **Each object joins as a block** (the vector path). Its names' bytes are
  one run in the runtime's name arena, copied at once, with their pointers
  moved eight at a time. Its atoms' units and fixup ranges move sixteen at a
  time and their bytes are copied one atom at a time: an executable patches
  atoms in place, so the runtime's bytes aren't shared. Its symbols that join
  new are compressed into place eight at a time, their ids an expand of
  consecutive ones, and the rest merge with the link's symbol of their name by
  scalar rules. Its fixups' targets move through the symbols' new ids by
  scalar loads, in a pass of their own. The exact path adds the same records
  one at a time through `link.c`'s checked adds.
- **Two definitions of a name** resolve as GNU ld and lld resolve them: a
  strong one takes a weak one's place, of two weak ones the first in link
  order stays (`Scrt1.o`'s, `crti.o`'s and `crtbeginS.o`'s come before the
  program's), and two strong ones are an error. So `crti.o`'s `_init` takes a
  weak `_init` of the program's over, and `Scrt1.o`'s weak `data_start` a weak
  one of the program's. The program defines its symbols as its atoms join,
  after `bind_begin`, so a definition of the runtime's that takes the
  program's place waits until `bind_end`. Whichever stays takes the most
  constraining visibility of every reference to the name and definition of
  it, as the gABI says.
- **Binding** (`bind_end`). The symbols that nothing defines are selected
  eight at a time by a compress over the atom, name and source columns, and
  each is looked up: the linker's names (`_GLOBAL_OFFSET_TABLE_` and the init
  and fini arrays' bounds), a crt object after the program, an export of
  `libc.so.6` (an import, with the export's type and version) but only for a
  reference with default visibility, a member of `libc_nonshared.a` for a
  strong reference, nothing for a weak one. A hidden, internal or protected
  reference needs a definition within the executable: GNU ld refuses a strong
  one that only a shared library defines and gives a weak one 0, and lld
  does the same.
  The members are a frontier walk over a bitset: each round binds the symbols
  the round before added. The exports come from the names that took a link
  symbol, as they took it, rather than from all of the runtime's.

Measured on the 9700X (`benchmark-bind`, one CPU, medians of 21 runs), for
generated programs that call libc, 200 workers per unit:

| Workers (global symbols) | Match, vector | Match, exact | `bind_begin`, vector / exact | `bind_end`, vector / exact |
| --- | ---: | ---: | ---: | ---: |
| 1 (18) | 0.09 µs | 0.07 µs | 0.28 / 0.40 µs | 0.23 / 0.20 µs |
| 64 (81) | 0.14 µs | 0.22 µs | 0.33 / 0.54 µs | 0.26 / 0.22 µs |
| 2,000 (2,080) | 1.83 µs, 0.88 ns per global | 4.71 µs, 2.26 ns | 2.05 / 5.06 µs | 0.73 / 0.69 µs |
| 102,400 (106,260) | 90.7 µs, 0.85 ns | 727 µs, 6.84 ns | 91.1 / 733 µs | 25.5 / 24.6 µs |

Before the filter, the vector match took 7.2 ns per global symbol at 102,400
workers, at an IPC of 1.1, against 2.0 ns at 2,000: the same instructions,
waiting on the index's lines. With it, 0.85 to 0.88 ns at both sizes, two
gathers per eight symbols. The crt objects' joins are a fixed 0.2 to 0.3 µs.
`bind_end`'s scan of the symbol columns for the names nothing defines runs as
fast scalar as in vectors (25 µs for 112,683 symbols, 0.22 ns each, the
scalar loop at an IPC of 7.5), both bound by reading the columns rather than
by instructions.
So the smallest program binds in 0.5 µs, well within hello world's 20 µs
target, and one of 102,400 workers in 117 µs, against 4 to 10 ms for its
whole link.

The dynamic PIE, the third part, is "The dynamic PIE" below: its stubs,
dynamic symbols, relocations and RELR, each pass with its vector path and
its exact twin. Where it differs from the plan here before it: the dynamic
symbols are found in bitsets, 64 symbols at a time, rather than compressed;
the stubs and refusals come from a filter in registers that every fixup's
target is tested against; RELR is encoded per atom, so that its size is known
before the layout; and `.gnu.hash`'s names are hashed one byte at a time,
since a program exports tens of them, with the dot product below left for
when binding exports thousands. `.gnu.hash`'s DJB hash, `h * 33 + c` per
byte, is a chain within a name but a dot product in closed form, `5381 *
33^n + sum(c[i] * 33^(n - 1 - i))` mod 2^32: a name of up to 64 bytes is
four `vpmulld` of its bytes widened to dwords by a vector of powers of 33
right-aligned to its length, and a reduction.

The snapshot (part 4) holds the runtime's columns as they are, written without
a record at a time and mapped back rather than read. The link's columns of
pointers stay offsets, which binding adds a delta to where it reads them. It
is validated by one `stat` per probe (19 here, 0.215 µs each, faster than
`statx`), its directory by a hash, and each piece by a hash of its own when
binding first reads it: "The runtime snapshot".

#### Layout

Segments in lld's order, which loaders and tools expect:

1. **R:** the ELF header, program headers, `.interp`, the notes, `.gnu.hash`,
   `.dynsym`, `.dynstr`, `.gnu.version`, `.gnu.version_r`, `.rela.dyn` and
   `.relr.dyn`, `.rodata`, `.eh_frame_hdr` and `.eh_frame`.
2. **RX:** `.text`, `.init` and `.fini`, in that order, as lld 23 writes
   them.
3. **RW, RELRO:** `.tdata` and `.tbss`, `.init_array`, `.fini_array`,
   `.data.rel.ro`, `.dynamic` and `.got`. The segment's memory size runs to
   the end of its page, as lld's `.relro_padding` does, so that
   `PT_GNU_RELRO` protects all of it.
4. **RW:** `.data` and `.bss`.

These are four `PT_LOAD`s, as lld writes for hello world, with `PT_PHDR`,
`PT_INTERP`, `PT_DYNAMIC`, `PT_GNU_RELRO`, `PT_GNU_EH_FRAME`, `PT_GNU_STACK`
(not executable), `PT_NOTE` and `PT_TLS` when there is TLS.

Each loaded segment's file offset equals its virtual address relative to the
image base. Its address continues at the next page plus its offset within a
page, preserving 4 KB congruence. The current four-segment layout adds at most
12 KiB of zero-filled file gaps, which the writer includes in the build ID.
This keeps the mapping base used by perf/libdw consistent across segments;
compact file offsets previously made native DWARF unwinding fail with
overlapping modules. See [the debug consumer checks](debug.md#checks).

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

The count and the placement go through the atoms 16 at a time
(`executable_atom_block`): a block of one bucket, as consecutive atoms mostly
are, of the classes whose alignment is their own (a function's raised to 16),
takes vectors. The count adds its sizes, padded to their alignment, and its
pieces; the placement scans them into its atoms' addresses, and when its
atoms are all in the file, or none, interleaves their pieces' records with
permutes, two to a vector. A block with a bucket's change, or an atom of the
classes that keep id order or are entries, goes one atom at a time, as the
exact twins do every block, which also finds any atom the layout refuses.
Then each symbol's reference, 16 at a time: a block whose symbols are all in
atoms that aren't thread-local gathers their atoms' classes and addresses.
On `benchmark-executable`'s 180,000 workers (185,457 atoms and 182,728
symbols), linked as a dynamic PIE by `benchmark-link --functions 180000` on
one lane, this took the count from 1.51 to 1.34 ms, the layout from 0.39 to
0.30 and the symbols from 0.54 to 0.31.

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
which most fixups are. Vector compares of an atom's kinds, 64 fixups at a
time, find whether it has any other (another kind, or an addend from the side
table). An atom without them goes through `executable_patch_pc32`, a loop
built for those two kinds. An atom with them goes through
`executable_patch_mixed`, which takes each fixup once, by its kind: `CALL`
and `PC32` as the loop does, the GOT kinds' G + A − P, `ABS64` and `TPOFF32`
inline, and the rest through `executable_patch_other`, which takes any kind.
Neither checks a `CALL`'s, `PC32`'s or GOT kind's value per fixup. The count
phase finds the largest addend magnitude with a vector maximum, and while the
base, the image and that addend stay below 2^31 together, no such value can
leave int32: G and P are in the image, and S too, or at address 0 (a weak
symbol defined nowhere). A static executable's absolute symbols can be
elsewhere, so the symbols' phase records each lane's lowest and highest
absolute reference, and the proof takes the span from the lowest S to the
image's end and from its start to the highest S, each with that addend.
`ABS64`'s 8 bytes take any
value, and `TPOFF32`'s offset from the thread pointer, which the TLS block
bounds and the image doesn't, is checked inline. Otherwise every fixup goes
through `executable_patch_other`, which checks each value's range. A value
that doesn't fit fails the link, naming the fixup, and its field is zeroed,
so that both paths leave the same bytes. `benchmark-executable`, ns per fixup
on CPU 2, on the synthetic cases of "SIMD" (every fixup `CALL` or `PC32`):

| Kernel | 64 KB, 2,000 fixups, 10,000 targets | 1 MB, 30,000, 100,000 | 64 MB, 2 M, 1 M |
| --- | ---: | ---: | ---: |
| `executable_patch_pc32` | **0.37** | **0.53** | **1.40** |
| a branch on the kind per fixup, the other kinds out of line | 0.73 | 0.74 | 1.42 |
| `executable_patch_other` for every fixup (the checked path) | 1.47 | 1.64 | 2.52 |

Clang unrolls `executable_patch_pc32`'s loop eight times and leaves the
branching one rolled; an earlier build of the same source measured the
branching one at 0.48 ns in L1. A compiled program's fixups are mostly `CALL`
and `PC32`, since the direct path reaches the program's own symbols directly.
The generated programs have the other kinds so that the tests cover every
kind: every worker of `benchmark-executable`'s 180,000 has 13 fixups, two
`CALL`s and five `PC32`s, then a `CALL_GOT`, a `LOAD_GOT`, two `TPOFF32`s, a
`GOTTPOFF` and an `ABS64`, 46% of the 2.34 million. Patching them all, one
atom after another (#212's measurements, one process, ns per fixup, with
cycles and instructions from the counters):

| Kernel | ns | Cycles | Instructions |
| --- | ---: | ---: | ---: |
| `executable_patch_pc32` for every fixup, then `executable_patch_other` again for the others (before #212) | 1.49–1.51 | 15.3 | 38.6 |
| `executable_patch_other` for every fixup (the checked path) | 1.37–1.45 | 14.6 | 44.9 |
| the loop or a branch on the kind per fixup, chosen per group of 64 | 1.38 | 14.4 | 42.2 |
| the same, chosen per atom | 1.27–1.30 | 13.9 | 39.9 |
| **a branch on the kind per fixup, the GOT kinds, `ABS64` and `TPOFF32` inline too (`executable_patch_mixed`'s loop), for every atom** | **0.97–0.98** | **12.1** | **31.4** |
| a branch-free kernel, each kind's terms from a table | 2.06–2.18 | 18.2 | 54.4 |
| 16 at a time: gathers, each kind's value under masks, scatters | 1.72–1.75 | 16.3 | 20.1 |
| `executable_patch_pc32` alone, which leaves the others wrong: the floor | 0.88 | 11.6 | 18.4 |

The branches on the kind cost almost nothing: the workers' fixups come in the
same order, and there are 0.006 branch misses per fixup. What the slower
kernels pay for is instructions, and the vector one for its gathers' and
scatters' cycles (17 for a `vpscatterdd`) over atoms of 13 fixups. Checking
every `PC32`'s range in the chosen kernel would cost 0.22 ns per fixup, more
than the count's pass over the addends that lets it skip the checks. In
`benchmark-executable` the kernel now takes 1.21 ns per fixup on that program
(1.65 before), and the write's chunks 6.61 ms (7.73).

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
   with `O_CREAT | O_EXCL` in its directory and rename it over the old one.
   The new file's name is its own, `.buster-executable-`, the process id and
   16 random hexadecimal digits: a suffix to the output's name could pass
   `NAME_MAX`, and the random digits keep a file a killed link left from
   blocking the next. If the output doesn't exist, create it.
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
  nowhere. A static executable needs no dynamic relocations. The generated
  programs use the GOT kinds to test them, where a compiled program's
  references to its own symbols are direct. (Step 4's executables relaxed
  nothing; since step 6, every kind relaxes a `CALL_GOT` or `LOAD_GOT` to the
  image's symbols, whose targets then need no slot: "GOT relaxation in every
  executable".)
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

(The dynamic PIE's part made the layout and symbols phases faster, and has
these programs' times again beside the PIEs': "The dynamic PIE".)

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

#### The dynamic PIE

Step 5's third part: `executable_write` writes a dynamic PIE against glibc
when `ExecutableOptions.dynamic` gives it what a static executable lacks
(`ExecutableDynamic`, which `dynamic_inputs` in `dynamic.c` fills from the
runtime and from binding): the interpreter GCC's driver names, `libc.so.6`
and its version definitions, `Scrt1.o`'s `.note.ABI-tag`, `crti.o`'s `_init`
and `_fini`, and the exports. The link is the program's bound by `bind.c`
("Step 5's phases, data-parallel"), so the writer looks up no name. The
programs run under the system's loader without stand-ins, and `test-dynamic`
compares each with lld's link of the same objects.

The writer also accepts foreign shared-library imports: `ExecutableDynamic`
borrows counted `ExecutableLibrary` and `ExecutableVersion` arrays until the
write returns. Library order is `DT_NEEDED` order. A required library is
already selected by the producer, including an unversioned dependency;
otherwise a used version selects it. Version descriptors are grouped by
library, have unique indices from 2 through 0x7fff, and use a 32-bit library
index. The runtime adapter keeps libc required and its loader conditional.
This removes the former two-library limit without putting name resolution
in the writer. Both `RUNTIME_SHARED` and `FOREIGN_SHARED` sources enter the
same exact and vector classification paths.

Library/version descriptors are validated before allocation. The existing
parallel symbol pass checks that every imported version above 1 has a
descriptor, using the output version-number array as a presence table until
the count barrier; no extra symbol pass or persistent column is needed.
Malformed arrays, names, duplicate version indices, unordered library groups, and missing
import versions fail before opening the output. Each lane uses two temporary
64-bit columns per library for needed-name offsets and version counts.
`test-dynamic` exercises three loaded dependencies across a 260-entry input,
including an unversioned library and versioned libraries above index 255,
with poisoned scratch, permuted names and exact/vector identity at 1/2/3/8
lanes. Actual foreign resolution and its handoff are a separate #86 slice.

On 23 October 2026, five alternating locked runs on the 9700X compared
`fc45e674d13d9257d66c02b05d78adf85ddae7ea` with the counted-input implementation
`41b2b9a05d1d95bfc20490386208b23e5823fa51` (before the additional malformed-version
checks), CPU 2, clang 23.1.1, frozen generated dynamic PIEs, tmpfs output.
The command was `ab --rounds 5 --cpu 2 --lock ~/buster-fast-bench/exclusive.sh
./baseline-compiler ./build/compiler -- benchmark-executable --only dynamic
--functions 1,2000,180000`. Medians of the five runs, in microseconds:

| Workers | Baseline in place / new | Counted inputs in place / new |
| ---: | ---: | ---: |
| 1 | 4.9 / 5.9 | 5.1 / 6.1 |
| 2,000 | 77.5 / 95.8 | 77.6 / 95.7 |
| 180,000 | 9,641.1 / 11,235.5 | 9,603.5 / 11,177.1 |

The small case adds about 0.2 microseconds of fixed validation/allocation
cost. Medium and large intervals overlap; these measurements establish the
cost of the feature, not a throughput improvement. Raw logs remain at
`~/buster-fast-bench/linker-libraries86/libraries-ab.log` on the approved host.

- **The file** is step 4's laid out at base 0 (`ET_DYN`, `DF_1_PIE`), with
  lld's sections and program headers besides. R gets `.interp`, the ABI note
  (one `PT_NOTE` with the build ID), `.dynsym`, `.gnu.version`,
  `.gnu.version_r`, `.gnu.hash`, `.dynstr`, `.rela.dyn` and `.relr.dyn`, and
  `.eh_frame` after `.rodata`; RX `.init`, `.fini` and `.plt.got`; RELRO
  `.dynamic` before `.got`. `PT_INTERP` and `PT_DYNAMIC` join the program
  headers. `.init`'s and `.fini`'s fragments and `.eh_frame`'s records keep
  link order, one after another as lld writes them. `.eh_frame_hdr` came with
  the generated programs' FDEs ("The unwind tables"); `.symtab` waits for
  debuggers.
- **Relocation classes.** Each symbol is RELATIVE (the image's: the load
  address adds to it), SYMBOLIC (an import, or a weak reference to nothing
  with default visibility, which a library may define: these are `.dynsym`'s
  undefined entries) or NONE (a TLS symbol's offset, a weak reference to
  nothing that isn't dynamic, which stays 0, and an absolute symbol, which
  stays its value, as GNU ld and lld leave it). A GOT slot of a RELATIVE target
  is relocated RELATIVE, of a SYMBOLIC one by `GLOB_DAT` (`TPOFF64` for a TLS
  import). An `ABS64` field in an atom the loader writes (`.tdata`, the init
  and fini arrays, `.data.rel.ro` and `.data`) is relocated RELATIVE, or by
  `R_X86_64_64` with the fixup's addend. Refused, naming the smallest fixup:
  an `ABS64` that needs a relocation in an atom the loader doesn't write (a
  text relocation), an `ABS32` or `ABS32S` that needs one, and a fixup of
  another kind to a dynamic symbol, as lld refuses them; and a `PC32` or
  `PC64` to a weak reference to nothing that isn't dynamic, which is at
  address 0 wherever the image loads, so that no field measured from the
  image reaches it. GNU ld refuses that `PC32` in code and read-only data
  ("can not be used when making a PIE object"); a `PC32` in writable data and
  a `PC64` anywhere, it and lld write as `0 - P` from the link's addresses,
  off by the load address. A `CALL`
  to it is patched so, as both linkers patch it: the program can only call it
  after checking that it has an address, which its absolute fields and GOT
  slot say (0, with no relocation). A `CALL`, `PC32` or `PC64` to an absolute
  symbol is refused for the same reason, as GNU ld and lld refuse it; its
  `ABS64` fields, GOT slot and `ABS32` fields need no relocation, wherever
  they are (GNU ld refuses the `ABS32`, lld takes it).
- **Libraries.** `DT_NEEDED` names `libc.so.6`, then the loader when an
  import is its (`__tls_get_addr`, `_r_debug` or `__libc_stack_end`), as GNU
  ld and lld take `libc.so`'s `AS_NEEDED`. `.gnu.version_r` has an entry per
  library with a needed version, all of them before their auxiliary entries,
  as lld lays them out. `test-dynamic` compares both, library by library,
  with lld's link.
- **Relaxation.** A `CALL_GOT` or `LOAD_GOT` with the addend −4 to a RELATIVE
  symbol (the image's) that isn't an IFUNC is relaxed, as lld relaxes it in a
  PIE ("GOT relaxation in every executable"), and its target gets no slot
  from it: the GOT keeps the dynamic symbols' slots (`GLOB_DAT`), those of
  the image's symbols that `GOTPCREL` reads (RELATIVE), and the
  thread-locals' offsets that initial-exec accesses read.
- **Stubs.** A `CALL` to a dynamic symbol (GCC 13's `crtbeginS.o` calls
  `__cxa_finalize` so, where GCC 16's reads the slot itself) goes to a stub
  in `.plt.got`, `jmp [rip + slot]` and a two-byte nop, GNU ld's form for a
  symbol whose GOT slot `GLOB_DAT` fills. So nothing binds lazily: there is no
  `.plt` or `.rela.plt`, as with `-z now`.
- **RELR.** With `GLIBC_ABI_DT_RELR` in `libc.so.6` (glibc 2.36 on, so the
  executable needs that version), the RELATIVE relocations at even addresses
  are `.relr.dyn`'s, the others `.rela.dyn`'s, which sort RELATIVE ones first
  for `DT_RELACOUNT`. To size `.relr.dyn` before the layout, each written
  atom's relocations are encoded from its first one's address, as if the atom
  started a run, so its words don't depend on where it lands; an address is
  even when the atom is aligned to 2 or more and the field's offset is even.
  The section is then the atoms' words in address order with the GOT's words
  among them: the GOT's marked symbols in a word of the bitsets have
  consecutive slots, so a word's RELATIVE slots are one address entry and at
  most one bitmap. The field holds the patched `S + A`, RELR's implicit
  addend. lld encodes the whole section as one run, so its `.relr.dyn` can be
  about a word per atom shorter.
- **`.gnu.hash`** has lld's shape: a bucket per four exports, at least one; a
  Bloom filter of 12 bits per export in the next power of two of 64-bit words
  above them, with shift 26; and the exports sorted by bucket (a stable
  counting sort) after the undefined symbols. The versions are numbered from
  2 in the libraries' order of them, `libc.so.6`'s first.

Every pass is data-parallel and vectorized, with an exact twin, scalar, that
`ExecutableOptions.exact` selects; the tests write each program both ways and
compare the files byte for byte:

| Pass (phase) | Vector path | Exact twin |
| --- | --- | --- |
| Symbols (count) | 64 symbols at a time from the source and flags columns, a mask per class: the SYMBOLIC and RELATIVE bitsets, and a 1,024-bit filter of the symbols outside the image (sources NONE and RUNTIME_SHARED). Names are read only in a word with a weak reference to nothing of default visibility | `executable_relocation_class` of each symbol |
| Fixups (count) | the targets of the fixups that neither read a GOT slot nor are `ABS64`, 16 at a time, against the filter in two registers (`vpmulld`, `vpermt2d`, `vpsrlvd`, `vptestmd`): a hit, a few hundredths of them, is looked up in the SYMBOLIC bitset, and a `CALL` to a dynamic symbol marks its stub and slot | each target's atom loaded |
| GOT (count) | the slots' classes by popcounts of the bitsets with the GOT's marks | each slot's class |
| Written atoms (count) | 64 atoms' classes looked up at once (`vpermb`); a dense atom (every fixup an `ABS64` to a RELATIVE target, 8 bytes apart from an even offset) checked 16 fixups at a time and its RELR words counted from its fixup count; the others one fixup at a time | every atom one fixup at a time |
| Slots (layout) | each word's slot numbers expanded into its marked lanes, 16 symbols at a time (`vpexpandd`) | one symbol at a time |
| `.rela.dyn` and `.relr.dyn` (dynamic) | each lane's ranges at their places by prefix sums; a dense atom's words from its first address and count, the GOT's by a `pext` of the bitsets, only the atoms with `.rela.dyn` relocations read again | every written atom's fixups encoded again |

The refusals are counted on the way (`ABS64` fixups against those written,
`ABS32` and `ABS32S`, and the filter's dynamic targets) and looked for, by an
exact scan for the smallest, only when the counts say there are some. Lane 0
writes the exports' `.dynsym` entries and `.gnu.hash`, `.gnu.version_r` and
`.dynamic`, which grow with the exports and the versions, not with the
program. `.gnu.hash`'s DJB hash is scalar per name: the exports are tens, and
the plan's dot product is for when binding exports thousands (`-rdynamic`).

`benchmark-executable` writes the same programs, bound against the system's
runtime, as dynamic PIEs, and the static executables again, in one session.
Medians in µs on the 9700X, the output on tmpfs, in place / as a new file,
against the floor (the least of its runs: at 400 KB it varies by half from
run to run):

| Program (file) | 1 lane | 2 lanes | 4 lanes | 8 lanes | Floor |
| --- | ---: | ---: | ---: | ---: | ---: |
| static, 1 worker (3.9 KB) | **3.0 / 3.7** | 5.1 / 6.0 | 4.9 / 4.9 | 7.2 / 6.6 | 0.6 / 1.4 |
| PIE, 1 worker (6.6 KB) | **4.4 / 5.4** | 8.1 / 7.1 | 5.8 / 6.9 | 8.3 / 9.5 | 0.7 / 1.6 |
| static, 64 workers (16 KB) | **5.1 / 6.6** | 6.4 / 8.0 | 6.7 / 8.5 | 7.7 / 9.6 | 0.8 / 2.3 |
| PIE, 64 workers (19 KB) | **6.4 / 7.9** | 8.4 / 9.6 | 8.0 / 9.9 | 9.2 / 11.2 | 0.9 / 2.3 |
| static, 2,000 workers (397 KB) | 68 / 85 | 48 / 69 | 44 / 65 | **44 / 64** | 9.7 / 24 |
| PIE, 2,000 workers (401 KB) | 73 / 89 | 50 / 68 | 46 / 67 | **45 / 67** | 8.6 / 24 |
| static, 180,000 workers (35.5 MB) | 8,400 / 9,620 | 4,860 / 5,620 | **4,060 / 5,620** | 4,200 / 5,910 | 2,320 / 4,240 |
| PIE, 180,000 workers (35.6 MB) | 8,640 / 10,010 | 5,050 / 5,720 | 4,280 / 5,660 | **4,240 / 5,580** | 2,290 / 4,210 |

The largest PIE has 184,557 atoms and 2.16 million fixups, ten dynamic
symbols, eleven `.rela.dyn` relocations, and 8,653 `.relr.dyn` words (69 KB)
for its 299,069 RELATIVE pointers and GOT slots, which `.rela.dyn` would take
7.2 MB for. Its phases in place, µs, against the static executable's:

| Lanes | | count | layout | symbols | dynamic | open | chunks | finish |
| ---: | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | static | 866 | 383 | 462 | | 8.1 | 6,670 | 2.4 |
| 1 | PIE | 1,107 | 411 | 481 | 15 | 8.1 | 6,610 | 2.6 |
| 8 | static | 328 | 120 | 83 | | 8.3 | 3,668 | 2.7 |
| 8 | PIE | 397 | 116 | 87 | 4.1 | 8.4 | 3,582 | 2.8 |

- **A PIE costs a few percent over a static executable,** 3% on one lane
  for 35.6 MB and 1% on eight. The dynamic phase is 15 µs; the count phase
  pays the rest. Its fixups pass reads the targets of the 1.44 million fixups
  that neither read a GOT slot nor are `ABS64`, 5.8 MB that the static count
  doesn't read, to find the calls to dynamic symbols: about 180 µs on one
  lane. The 180,000 pointers of `buster_functions` are one atom, whose dense
  check takes 49 µs on whichever lane has it, which the other seven wait for.
  The small PIEs pay their larger files and sections: 1.4 µs more for one
  worker.
- **A filter has false positives.** The first version took a dense atom to be
  one whose targets all miss the filter. With about ten symbols outside the
  image, 1% of all targets hit it anyway, so no table of thousands of
  pointers was dense, and three passes went through `buster_functions` one
  fixup at a time: 1.16 ms of dynamic phase on one lane and 0.71 ms on eight.
  Looking each hit up in the RELATIVE bitset instead made it 15 µs and 4 µs.
  `test-dynamic` writes a program whose table is sure to have such hits.
- **The symbols pass reads 2 bytes a symbol.** The source and flags columns
  give every class, `link_check` making a symbol of source NONE atomless and
  a TLS symbol one by its type. Reading the atom and name columns too took
  61 µs for 190,858 symbols on one lane, from DRAM; this takes 13.
- **The static path got faster with it.** `executable_count`,
  `executable_place` and `executable_resolve` keep their columns in locals,
  which their stores can't alias, and the first two a run of atoms of one
  bucket in registers, where the sums in memory chained each atom's add to
  the last one's store. In a session that
  ran both, 180,000 workers took 8,250 to 8,330 µs on one lane where step 4's
  code took 8,550 to 8,630 (layout 390 µs against 490, symbols 470 against
  585), and 2,000 workers 68.5 µs against 70; one worker took 2.9 to 3.0 µs
  against 2.7 to 2.8, the dynamic sections having made the section table
  twice as long.

#### The static executable with libc

Step 6's third part: `executable_write` writes a static executable that links
the C runtime when `ExecutableOptions.runtime` gives it what one without
lacks (`ExecutableRuntime`, which `dynamic_static_inputs` in `dynamic.c`
fills): `crt1.o`'s ABI note, and the link's `__tls_get_addr`, which nothing
in a static link defines. The link is the program's bound against the static
runtime ("Static links, as built"), so `crt1.o`'s `_start` is the entry and
`libc.a`'s startup does what a loader would: it applies the IRELATIVE
relocations, sets up TLS from `PT_TLS` and runs the init and fini arrays.
`test-static` runs the generated programs, and twelve compiled ones (hello,
stdio, threads, `setjmp` and signals, a weak `puts`, prioritized
constructors, decimal floating point from a thread, GCC 14's heap
trampolines, and assembly of every TLS sequence, of local and global IFUNCs,
of a weak IFUNC defined nowhere and of absolute symbols) as `cc -static`'s
link of the same object runs, and with `--cross-check` as lld's.

- **The file** is step 4's at 0x400000 (`ET_EXEC`), with `.note.ABI-tag` in
  the R segment (one `PT_NOTE` with the build ID), `.init` and `.fini`
  after `.text` from the crt objects' fragments in link order, as for a
  PIE, and `.rela.iplt` in R and `.iplt` in RX for the IFUNCs. No
  `PT_INTERP` or `PT_DYNAMIC`.
- **IFUNCs** (glibc's string and memory functions): an IFUNC that a fixup
  reaches other than through the GOT gets a stub in `.iplt`, `jmp [rip +
  slot]` and a two-byte nop, which calls reach it through. A fixup that takes
  its address (any kind but a call: lld's direct relocation) makes the stub
  stand for it everywhere, as lld's canonical PLT entry does, so that `&f` is
  the same however it is taken: its GOT slot, if it has one, holds the stub's
  address. When only calls reach the stub, the IFUNC's loads through the GOT
  read the stub's slot instead ("GOT relaxation in every executable"). Each
  stub's slot comes after the GOT kinds' and an IRELATIVE relocation in
  `.rela.iplt` fills it at startup from the IFUNC's resolver, its addend; an
  IFUNC reached only through the GOT gets an IRELATIVE relocation of its GOT
  slot instead. `__rela_iplt_start` and `__rela_iplt_end` are the section's
  bounds. The generated programs have 18 stubs and 22 IRELATIVE
  relocations, and each compiled one as many IRELATIVE relocations as lld's
  link of it: one per IFUNC it reaches. An IFUNC is one with a resolver: a
  weak IFUNC defined nowhere is address 0, as any weak symbol defined
  nowhere is, and GNU ld links it so. lld 23 makes an IRELATIVE relocation of
  it with resolver 0, whose program faults at startup.
- **TLS:** `libgcc.a`'s PIC members (decimal floating point, split stacks)
  reach their thread-locals by general-dynamic accesses and GCC 14's heap
  trampolines by local-dynamic ones, which call `__tls_get_addr`. As GNU ld
  and lld do in an executable, the writer relaxes each to local-exec as it
  patches: the general-dynamic sequence (`data16 lea x@tlsgd(%rip), %rdi`,
  then a call through the PLT or the GOT, 16 bytes) becomes `mov %fs:0, %rax;
  lea x@tpoff(%rax), %rax`, and the local-dynamic one `mov %fs:0, %rax` with
  0x66 prefixes, so that its `DTPOFF32` fields, which then add to the thread
  pointer, hold offsets from it. The call's fixup patches nothing, and
  `__tls_get_addr` gets no slot. A sequence that isn't one the linkers take,
  or a call of `__tls_get_addr` outside one, is refused (the count compares
  the accesses with the calls). Binutils 2.42 writes the general-dynamic
  access's prefixed call through the GOT as `GOTPCREL`, so the relaxation
  takes it as it takes `CALL_GOT`. A chunk boundary never splits a sequence,
  which starts before its fixup's field.
- **Prioritized arrays** (`.init_array.N` and `.fini_array.N`, from the
  priorities table): their atoms go first in their sections, by priority and
  in id order for equal ones, as GNU ld and lld put them, then the others in
  id order. The count leaves them out of the buckets, and lane 0 places them
  after the other lanes' atoms by a stable radix sort.
- **Names:** besides the arrays' bounds and `_GLOBAL_OFFSET_TABLE_`, the
  linker defines the names `libc.a`'s startup looks for: `__ehdr_start` and
  `__executable_start` (the ELF header), `__preinit_array_start` and
  `__preinit_array_end` (at `.init_array`'s start: there is no
  `.preinit_array`), the IRELATIVE
  relocations' bounds, `_etext`, `etext` and `__etext` (the RX segment's
  end), `_edata` and `edata` (the end of the sections with bytes in the
  file, lld's), `__bss_start`, and `_end` and `end` (the image's end).
- **Absolute symbols** (glibc's `_nl_current_LC_CTYPE_used` and the like) are
  their values, as image offsets in the references column, which can't hold
  one farther than int32 from the base: such a symbol is refused. A `CALL` or
  `PC32` can still be farther from one than int32 reaches, which the patch's
  range proof takes in ("Patching and writing"): the link then takes the
  checked path, which refuses the fixup, as GNU ld says the relocation is
  truncated to fit.

The passes it adds, each data-parallel with an exact twin that
`ExecutableOptions.exact` selects, which the tests compare byte for byte:

| Pass (phase) | Vector path | Exact twin |
| --- | --- | --- |
| IFUNCs (count) | 64 symbols' types compared at a time, and the atoms of those of the type 16 at a time (a weak IFUNC defined nowhere has no resolver), a word of a bitset of the IFUNCs, and a filter of them in four registers, two hashes | each symbol's type and atom |
| Stubs (count) | the targets of the fixups that don't read a GOT slot, 16 at a time, against the filter: a target is taken only if both its bits are set, and a hit is looked up in the bitset | every target's type and atom |
| TLS accesses (count) | the accesses by compares of 64 kinds, and the calls of `__tls_get_addr` by compares of 16 targets | the same compares, which are the check itself |
| IRELATIVE relocations (dynamic) | each lane's range of the GOT's words, the IFUNCs' slots from the bitsets, at their places by the lanes' counts | each slot's type and atom |

With one hash, as a PIE's filter has, the IFUNC filter took a 46th of the
largest program's 1.81 million targets for IFUNCs on the 9700X (39,664, in a
link of 37 IFUNCs that 258 of them reach), each one's exact check a few
nanoseconds of the count; with a second hash in two registers of its own it
takes 1,613, and the count phase went from 1,588 to 1,309 µs on one lane and
from 738 to 721 on eight. Restricting the test to the IFUNCs' range of symbol
ids did nothing: binding gives one symbol per global name, so the program's
own `strlen`, low in id order, is the IFUNC a member defines.

Two parts stay scalar, measured on the 9700X with a probe of the compiled
programs (one lane, medians of 41 runs):

- **The relaxation** is a compare and a copy per access, in the patch's path
  for the kinds other than `CALL` and `PC32`, which takes each fixup by its
  kind anyway. The accesses are few: none in the generated programs or in
  hello; the decimal program, which reaches the most, has 47, in five atoms,
  whose 340 fixups the patch takes in 0.89 µs of its 125 µs write.
- **The arrays' sort** is lane 0's, before the chunks: a stable radix sort of
  the prioritized entries, three passes of eight bits. The static runtime's
  2,622 members have two, and a program a few: the priorities program's six
  take 0.24 µs of its 99.5 µs write.

`benchmark-executable` writes the generated programs bound against the
static runtime as static executables, with the static ones without libc and
the PIEs of "The dynamic PIE" in the same session. Medians in µs on the
9700X, the output on tmpfs, in place / as a new file, against the floor (one
`pwrite` of the same bytes, opened and closed):

| Program (file) | 1 lane | 2 lanes | 4 lanes | 8 lanes | Floor |
| --- | ---: | ---: | ---: | ---: | ---: |
| static with libc, 1 worker (730 KB) | 106 / 129 | 64 / 104 | **52 / 107** | 52 / 116 | 18 / 44 |
| static with libc, 64 workers (746 KB) | 100 / 130 | 65 / 102 | **55 / 114** | 55 / 117 | 19 / 45 |
| static with libc, 2,000 workers (1.24 MB) | 173 / 215 | 117 / 180 | 81 / 181 | **76 / 173** | 31 / 75 |
| static with libc, 180,000 workers (46.3 MB) | 9,980 / 11,690 | 6,600 / 7,710 | **6,160 / 7,850** | 6,300 / 8,140 | 3,240 / 5,640 |
| static, 180,000 workers (45.6 MB) | 8,880 / 10,620 | 5,820 / 7,290 | **5,250 / 7,280** | 5,270 / 7,720 | 3,380 / 5,560 |
| PIE, 180,000 workers (45.7 MB) | 9,220 / 10,960 | 6,020 / 7,040 | **5,400 / 7,630** | 5,670 / 8,080 | 3,160 / 5,540 |

The programs join 442 of the static runtime's members: 2,323 atoms and
12,062 fixups for one worker, 730 KB of the file. The phases in place, µs:

| Program | Lanes | count | layout | symbols | dynamic | open | chunks | finish |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 1 worker | 1 | 15.7 | 6.7 | 3.7 | 4.3 | 4.8 | 67.5 | 0.5 |
| 1 worker | 8 | 4.4 | 2.2 | 1.0 | 0.9 | 4.7 | 38.8 | 0.5 |
| 180,000 workers | 1 | 1,309 | 307 | 331 | 163 | 622 | 7,234 | 2.6 |
| 180,000 workers | 8 | 721 | 185 | 100 | 52 | 660 | 4,618 | 3.0 |

- **libc's 730 KB are most of a small program's cost:** 106 µs on one lane in
  place, the chunks 68 of it, where a PIE of one worker, with libc outside
  the image, takes 4.7. Lanes pay from here: four write it in place in
  52 µs. As a new file it takes 104 to 129 µs whatever the lanes, where the
  floor's new file takes 44 µs against 18 in place.
- **The unwind table is out of order, and lane 0 sorts it.** The static
  runtime's members have their functions' hot parts in `.text` and the cold
  ones in `.text.unlikely`, and GCC writes each function's two FDEs one after
  the other, so a member's table entries interleave its two atoms. The sorted
  check fails, and the radix sort (3.4 ns per entry) is most of the open
  phase: 4 µs of one worker's write, 600 µs of 180,000 workers', on one lane
  whatever the lane count. A partition of the entries by `.text`'s buckets,
  the other cause of disorder, was tried before the sort: it leaves the
  hot and cold parts interleaved within a bucket, so the radix sort still
  ran, after it (open: 6.7 µs and 805 µs). Placing each entry by its
  function's atom, a counting sort that the count and table passes could do
  by lanes, would sort them without the radix sort, and is left for later.
- **The largest program costs 8% more than its PIE** on one lane: the sort,
  and the chunks' 700 KB more of libc.

#### The static PIE

Step 6's fifth part, its first half: `executable_write` writes a static PIE
when `ExecutableOptions` has both `dynamic` and `runtime`
(`dynamic_static_pie_inputs` in `dynamic.c`), from a link bound against the
static PIE's runtime (`runtime_import_static_pie`: `rcrt1.o`, `crti.o`,
`crtbeginS.o`, `crtendS.o` and `crtn.o` around the static executable's
archives). It is a PIE without dynamic symbols that `libc.a`'s startup
relocates itself: `rcrt1.o` leaves `_dl_relocate_static_pie` to `libc.a`'s
member, which finds `.dynamic` through `_DYNAMIC` and applies its relocations
before the program reads a relocated address. `test-static` runs every
program as a static PIE as well, and with `--cross-check` against lld's.

- **The file** is the dynamic PIE's at base 0 (`ET_DYN`, `DF_1_PIE`) without
  `.interp`, `PT_INTERP`, `DT_NEEDED` or versions: lld's sections, program
  headers and `.dynamic` tags. `.dynsym` holds the null symbol alone and
  `.gnu.hash` no symbol, as lld writes them; glibc's self-relocation reads
  `DT_SYMTAB`'s first entry for its IRELATIVE relocations' symbol. The R
  segment has `rcrt1.o`'s ABI note, the RX segment `.init`, `.fini` and the
  IFUNCs' stubs in `.iplt`, and RELRO `.dynamic` and `.got`.
- **Relocation classes:** RELATIVE or NONE, since no symbol is dynamic. A
  weak reference to nothing is address 0, as GNU ld and lld leave it in a
  static PIE, so a `PC32` or `PC64` to one is refused, and so are text
  relocations, an `ABS32` to the image and a `CALL`, `PC32` or `PC64` to an
  absolute symbol, as in a dynamic PIE.
- **IFUNCs:** as the static executable's, stubs in `.iplt` that calls reach
  and that stand for their addresses everywhere when a fixup takes one, their
  slots and the GOT slots of IFUNCs without stubs filled by IRELATIVE
  relocations, which go last in `.rela.dyn`; a GOT slot that holds a stub's
  address is RELATIVE (an IFUNC whose stub only calls reach reads the stub's
  slot: "GOT relaxation in every executable"). lld's static PIE keeps the
  same pointer equality; GNU ld's gives a pointer in `.data` the IFUNC's
  implementation where a `lea` gives its `.iplt` entry, and `test-static`'s
  IFUNC program sees them differ there.
- **GOT relaxation, which a static PIE needs.** `rcrt1.o`'s `_start` calls
  `__libc_start_main` through its GOT slot (`call [rip + slot]`), and
  `libc.a`'s code before the self-relocation reads others: the slots hold no
  run-time addresses yet (0, in `.rela.dyn`'s form). GNU ld's and lld's static
  PIEs of hello world linked with `--no-relax` fault there. So a `CALL_GOT`
  or `LOAD_GOT` with the addend -4 to a symbol of the image's that isn't an
  IFUNC is relaxed as both linkers relax it: `call [rip + slot]` to
  `addr32 call` (`67 e8`), `jmp [rip + slot]` to `jmp` and a `nop`,
  `mov r64, [rip + slot]` to `lea`, and its target gets no slot unless
  another reference reads one. The import gives those kinds only those
  instructions ("Fixups from the encoder"), so the patch rewrites only what
  it may, and the rewrite takes its own forms too, so that a link written
  again over its relaxed bytes comes out the same (`test-static` writes each
  static PIE so once more). A chunk boundary never splits the two bytes
  before such a field either. The generated program of one worker had 155 GOT
  slots as a static executable and 62 as a static PIE; every kind relaxes them
  now ("GOT relaxation in every executable").
- **RELR**, when `libc.a`'s startup takes `DT_RELR`: glibc's
  `_dl_relocate_static_pie` applies it from 2.36 on, before `DT_RELA`, and an
  older one would leave its relocations undone. `libc.a` has no version
  definitions to say so, as `libc.so.6`'s `GLIBC_ABI_DT_RELR` does for a
  dynamic PIE, so the writer takes `arc4random`, which glibc added in that
  release, as the sign (`dynamic_static_relr`). `cc -static-pie` writes RELA
  unless given `-z pack-relative-relocs`; both linkers' RELR static PIEs run
  on the 9700X. The program of 180,000 workers had 183,839 RELATIVE
  relocations in `.rela.dyn` (4.4 MB) without RELR, a 49.8 MB file whose
  write took 12.3 ms on one lane; with RELR, a 45.4 MB file.

Against lld's static PIE of each compiled program (`--cross-check`, lld given
`-z pack-relative-relocs` as ours has RELR), the IRELATIVE relocations are as
many and the RELATIVE ones outside the GOT the same (1,099 for hello world);
ours had 6 more in the GOT, the slots of IFUNCs that only calls give stubs,
which lld points at the stubs' IRELATIVE slots. They are gone ("GOT
relaxation in every executable").

`benchmark-executable` writes the generated programs as static PIEs
(`--only static-pie`). Medians in µs on the 9700X, the output on tmpfs, in
place / as a new file, against the static executables of the same session:

| Program | static, 1 lane | static PIE, 1 lane | static, 8 lanes | static PIE, 8 lanes |
| --- | ---: | ---: | ---: | ---: |
| 1 worker | 101 / 133 | 108 / 138 | 55 / 113 | 58 / 117 |
| 64 workers | 102 / 133 | 111 / 140 | 54 / 115 | 56 / 118 |
| 2,000 workers | 180 / 225 | 184 / 232 | 76 / 179 | 78 / 183 |
| 180,000 workers | 10,500 / 12,140 | 10,550 / 12,140 | 6,610 / 8,580 | 6,500 / 8,750 |

- **A static PIE costs 0.5% to 9% more than a static executable on one
  lane,** 7 to 9 µs for the small programs: one worker's count takes 21.3 µs
  against 16.5 (its symbols' classes, the written atoms' relocations and the
  targets' checks for both kinds) and its dynamic phase 6.5 against 4.1
  (`.dynamic`, `.dynsym`, `.relr.dyn`); 180,000 workers' count 1,564 against
  1,332, and its symbols' phase 234 against 326 (69 GOT slots to fill, not
  116,498).
- **The count took 2.6 ms at first** for 180,000 workers on one lane, where
  the static executable's takes 1.33 and the dynamic PIE's 1.38. Without RELR,
  the written atoms' relocations went one fixup at a time (the dense atoms'
  path is RELR's): RELR took the count to 2.20 ms, and the dynamic phase from
  558 µs to 185. The relaxation looked up each candidate's target in the
  bitset of relaxable symbols, 361,309 of the program's 541,519 reads of a
  slot: marking the candidates in a bitset of their own, which the reduction
  ANDs with the relaxable symbols a word at a time, took it to 2.01 ms
  (gathers of the bitset's words took it to 2.40: each costs about 13 cycles
  whatever its mask). And the PIE's filter of the symbols outside the image,
  by one hash, had 55 of them in a static PIE where a dynamic one has 10, and
  a 33rd of the targets hit their bits (49,000 of 1.6 million, mostly the
  image's symbols): testing the targets against the IFUNCs' filter of two
  hashes, with the symbols outside the image added to it, for both checks at
  once took it to 1.56 ms. Each hit's check also ends at its bit in the
  RELATIVE bitset now, before reading the symbol's columns, in both PIEs.

#### GOT relaxation in every executable

Step 6's part after the static PIE: every kind of executable relaxes its
loads and calls through the GOT as GNU ld and lld relax them in an
executable, where #225 relaxed only the static PIE's, which can't run without
it. A `CALL_GOT` or `LOAD_GOT` with the addend −4 to a symbol of the image's
that isn't an IFUNC is rewritten as "The static PIE" says (`addr32 call`,
`jmp` and a `nop`, `lea`), and its target gets no slot unless a fixup reads
the slot unrelaxed: a `GOTPCREL` (`push [rip + slot]`, `libgcc.a`'s adds of
slots), a `GOTTPOFF`, or a load of an IFUNC.

- **The rule is lld's.** lld relaxes a `GOTPCRELX` or `REX_GOTPCRELX` with the
  addend −4 to a symbol that isn't preemptible, an IFUNC, absolute,
  thread-local or undefined (its `RelocScan::process` and
  `X86_64::adjustGotPcExpr`). In an executable no definition is preemptible,
  so that is relocation class RELATIVE but the IFUNCs, with the linker's names
  among them: `executable_relocation_class`'s for a PIE, and with no imports
  for a static executable. `-shared` will leave its preemptible definitions
  out of the same bitset. lld's non-PIC immediates for `test` and binops
  through the GOT (`R_RELAX_GOT_PC_NOPIC`) aren't taken: the import gives
  those instructions `GOTPCREL`, and the count doesn't read the code.
- **The passes,** data-parallel with exact twins: `executable_mark_relaxable`
  makes the bitset of the relaxable symbols 64 at a time, a PIE's from its
  RELATIVE bitset and a static executable's from compares of the source and
  type columns, 2 bytes a symbol, less the IFUNCs (the static runtime's
  bitset, or a compare of the types and, for those of the type, of the
  atoms); its twin classifies one symbol at a time. The count marks the loads'
  and calls' targets as candidates, and the reduction ANDs them with the
  bitset 64 symbols at a time: #225's passes, now for every kind. A chunk
  boundary keeps a relaxing instruction's opcode and ModRM in its field's
  chunk, for every kind: `test-executable` puts a relaxing `call [rip +
  slot]` across a boundary at every place.
- **An IFUNC whose stub only calls reach shares the stub's slot.** A stub
  stands for its IFUNC's address everywhere when a fixup takes the address
  (any kind but a call and the GOT kinds: lld's `HAS_DIRECT_RELOC`), and a GOT
  slot of the IFUNC then holds the stub's address, as before. When only calls
  reach the stub, nothing takes it for the address: the IFUNC's loads through
  the GOT read the stub's own slot, which its IRELATIVE relocation fills with
  the IFUNC's code, as lld's read its `.got.plt` entry (`gotInIgot`). That is a
  slot fewer, and in a static PIE a RELATIVE relocation fewer. The count tells
  the calls from the other fixups among the IFUNC filter's hits, a bitset per
  lane; the reduction takes the IFUNCs whose stubs only calls reach out of the
  GOT's marks, and the placement gives them their stubs' slots. `test-static`'s
  `ifunc-calls` program calls an IFUNC directly and through its slot, and
  loads it.

On the 9700X, against main at 44e1189, where only the static PIE relaxed, on
one lane (CPU 2), medians of three interleaved runs of each, in µs.
`benchmark-executable` writes each program over its own last bytes, whose
loads and calls it relaxed already:

| 180,000 workers | Static | Dynamic PIE | Static, with libc | Static PIE |
| --- | ---: | ---: | ---: | ---: |
| GOT slots | 116,338 fewer | 116,339 fewer | 116,498 → 63 | 69 → 64 |
| The file | 45.61 → 44.67 MB | 45.68 → 44.70 MB | 46.33 → 45.40 MB | 45.43 MB, 64 bytes less |
| The write, in place | 9,403 → 9,302 | 9,771 → 9,596 | 10,454 → 10,308 | 10,619 → 10,653 |
| — its symbols | 299 → 192 | 324 → 220 | 320 → 206 | 235 → 234 |
| — its chunks | 7,530 → 7,492 | 7,547 → 7,438 | 7,712 → 7,651 | 7,700 → 7,684 |
| — its count | 1,103 → 1,163 | 1,410 → 1,464 | 1,328 → 1,365 | 1,568 → 1,616 |

`benchmark-link`'s direct path binds the program afresh before each write,
copying its code as a compiler's link does:

| Direct path | 1 worker | 2,000 workers | Synth's shape | — its chunks |
| --- | ---: | ---: | ---: | ---: |
| Dynamic PIE | 15.6 → 15.9 | 86.3 → 85.5 | 6,945 → 7,200 | 5,544 → 5,792 |
| Static | 288 → 286 | 361 → 364 | 7,911 → 8,092 | 5,851 → 6,080 |
| Static PIE | 298 → 297 | 377 → 380 | 8,182 → 8,213 | 6,027 → 6,039 |

- **The slots go, and what fills them.** The symbols phase fills 116,000
  fewer slots, 0.1 ms less at 180,000 workers, and the file is 0.93 MB
  smaller (the dynamic PIE's RELR words go from 8,653 to 2,916). The static
  executable of synth's shape keeps 63 slots where it kept 66,335, and comes
  to 44.13 MB, 23 KB less than the static PIE.
- **The count takes 0.04 to 0.06 ms more** at 180,000 workers: the bitset of
  the relaxable symbols, and the candidates' row of their own. Between builds
  whose count is the same code it moves by as much.
- **A compiler's link pays for reading each relaxed call's opcode.** The
  patch reads it to tell `call` from `jmp`, which `CALL_GOT` doesn't say:
  that is its only load from the code, where every other fixup only stores
  its field (a `LOAD_GOT`'s rewrite stores too). When the code comes fresh
  from binding, the call's line is often out of L2 by the chunks, and the
  load holds up retirement where a store's miss doesn't: at synth's shape,
  102,400 relaxed calls cost the chunks 0.23 and 0.25 ms, more than the
  smaller file saves, and the static executable's and the dynamic PIE's links
  take 2.3% and 3.7% longer. A write over relaxed bytes pays almost nothing
  for them: the patch kernel over the 180,000 workers' fixups, by itself,
  takes 1.638 ns a fixup relaxing them from the code generator's instructions
  (put back before each run), 1.587 over relaxed bytes and 1.662 without
  relaxing them, its lines in cache.
- **What was tried.** Prefetching the calls' lines eight pieces ahead in the
  chunks (a compare of each piece's kinds, then a prefetch of each call's
  line) took the static executable of synth's shape to 7,946 µs and the static
  PIE to 8,064, in one run of each, but its bookkeeping cost 3% to
  8% on the small links (a static hello world's is 2,300 pieces, most of them
  `libc.a`'s members' atoms) and 2% to 4% on writes over relaxed bytes at
  180,000 workers. A 16-bit read of the opcode, with a call's or a jmp's 6
  bytes as two stores rather than three, gave nothing beyond the noise
  between builds, and nor did dropping the count's mask of calls (reading the
  kind on an IFUNC filter's hit instead). None is taken. A kind of its own for
  `jmp [rip + slot]`, which the import and `link_add_code_fixups` know, would
  let the patch store without reading: left for later.
- **What is left of lld's:** initial-exec TLS accesses relaxed to local-exec
  (`GOTTPOFF`), whose slots stay, 3 in hello world's static PIE and 4 in the
  decimal program's; and in a static executable, the non-PIC immediates for
  `test` and binops through the GOT, 5 slots in the decimal program.

#### The shared library

`executable_write` writes a shared library when `ExecutableDynamic.shared` is
set. Binding uses `runtime_open_shared` and `dynamic_shared_inputs`: the C
runtime's shared-library inputs without a start object, ABI note or interpreter.
`ExecutableDynamic.soname`, when present, becomes `DT_SONAME`. The file is
ET_DYN, keeps PT_PHDR, has no PT_INTERP, and uses entry zero unless the link
explicitly provides one. Initial-exec TLS adds DT_FLAGS with DF_STATIC_TLS;
the table has neither a PIE's DT_FLAGS_1 nor DT_DEBUG.

A regular shared-library output is always written to a new file in its
directory and renamed into place. It never overwrites the previous inode:
processes can map a library without ETXTBSY protecting its file. Existing
mappings keep the old library's bytes while new opens see the replacement.
Special output files retain the writer's sequential-output rules.

**Preemption.** A global or weak definition with default visibility is dynamic
and preemptible, like an undefined default-visibility reference left for the
loader by `-z undefs`. A protected or local definition remains in the image.
Preemptible symbols never participate in GOT relaxation. Their GOT slots use
R_X86_64_GLOB_DAT, pointer fields use R_X86_64_64, and direct calls go through a
non-lazy stub, `jmp [rip + slot]`, with a GLOB_DAT slot. This is lld's `.plt.got`
rule rather than its lazy PLT. Other direct references to such symbols are
refused, since no permitted relocation can preserve their meaning in the
read-only image.

**Thread-local storage.** Initial-exec references to preemptible TLS use
R_X86_64_TPOFF64 against that dynamic symbol. The library's own non-preemptible
TLS uses a TPOFF64 slot against symbol zero, with its offset within PT_TLS as
the addend. Either initial-exec form sets DF_STATIC_TLS. Local-exec TPOFF32 is
refused, as lld refuses it in a shared library.

General-dynamic TLS keeps `__tls_get_addr` calls and allocates a two-word GOT
pair per referenced symbol, separately from initial-exec slots so both models
can use the same symbol. Preemptible definitions and imports use DTPMOD64 and
DTPOFF64 relocations against the dynamic symbol. A local definition uses
DTPMOD64 against symbol zero and a statically filled offset within PT_TLS.
Local-dynamic accesses share one pair: DTPMOD64 against symbol zero followed
by zero, with DTPOFF32 fields holding the definition's module offset.
Undefined DTPOFF32 targets are refused before publication. An imported-only
TLS library does not acquire an empty PT_TLS segment, and general/local
dynamic accesses do not set DF_STATIC_TLS.
An undefined hidden weak TLSGD target is also refused: it has neither a
defining module nor a dynamic lookup. In an original standalone reference
probe on the 9700X, GNU ld 2.47 and lld 23.1.1 both emitted DTPMOD64 against
symbol zero without PT_TLS; the loaded address was `0x2`, not a valid TLS
definition. The writer does not manufacture a current-module address for it.

Each lane marks general-dynamic targets in its own bitset. A reduction owns
complete symbol words, then mask expansion assigns pairs after ordinary GOT
slots in stable symbol order; an exact scalar twin assigns the same slots.
The local-dynamic pair follows all general-dynamic pairs. No symbol or fixup
is reordered. Additional temporary storage is
`8 * lanes * (symbols / 64 + 1) + 4 * symbols` bytes plus counts; the borrowed
columns remain live through the write, and vector tails use logical counts.
The snapshot format is unchanged.

**Exports across lanes.** Each lane hashes and counts its exports; layout
prefixes the bucket counts in bucket order and then lane order; the dynamic
phase fills each lane's disjoint names, symbols and hash chains. Bloom words
and bucket ranges are reduced without concurrent writes to a shared word.
The result is byte-identical at every lane count. Absolute-field classification
uses the relative and dynamic bitsets, with an exact scalar twin. Direct-fixup
filter hits retain the per-symbol check, including absolute-call refusal.

**Rejected direct-check experiment.** The 16-lane gathered/compressed check
from `dc0fa91`, corrected for absolute calls at `c42af0b`, was compared with
the prior per-hit check on identical frozen source and inputs. Seven alternating
processes per build on CPU 2 measured the 180,000-worker shared library:

| Old in-place file-policy row | Per-hit check | Corrected 16-lane check |
| --- | ---: | ---: |
| Count phase, median | 4.190 ms | 5.521 ms |
| Count phase, process range | 4.188–4.207 ms | 5.513–5.536 ms |
| Whole write, median | 17.251 ms | 18.581 ms |

The count cost rose 31.8%, and the whole write 7.7%, beyond process spread.
The new-file row also lost, and the eight-lane results gave no compensating
win. The direct-check experiment was removed; its absolute-call regression
remains. These are historical measurements before the required always-rename
file policy; they are not the final writer's timings.

**Checks and measurement.** `test-dynamic` writes generated and compiled
libraries on 1, 2, 3 and 8 lanes and through the exact paths, requires identical
bytes, checks their ELF and TLS rules, and loads and runs them with the
generated `dlopen` harness. Compiled fixtures check executable definitions
preempting library calls, addresses, data and tables. The lld cross-check links
the same objects with `-shared` and accounts for the lazy/non-lazy slot
difference. Refusal cases cover local-exec TLS, text relocations and a direct
address of a preemptible function and undefined DTPOFF32 module offsets.
Compiled TLS fixtures cover general/local dynamic access, hidden and
preemptible definitions, zero fill, 73 TLS symbols, mixed general-/initial-exec
references to one symbol, GOT-indirect `__tls_get_addr`, imported libc TLS,
and a thread created before `dlopen`. `benchmark-executable --only shared`
measures the writer by
phase; `benchmark-link --kinds shared` includes runtime opening and binding,
compares GNU ld, lld, mold and wild, and runs every resulting library through
the harness. `benchmark-executable --only shared --tls general|local`
adds one distinct TLS symbol and a callable ABI probe per generated worker;
the benchmark loads the resulting library and executes those probes outside
the timed region. The distinct symbols cycle over at most 64 backing TLS
objects, keeping the existing initial-exec workload within `dlopen`'s static
TLS reserve. This measures GOT-pair and fixup scaling, not initialization of
a large TLS storage block. The output records both counts.
`--exact` selects the scalar writer twin and
`--after-scalar MS` conditions each lane with a bounded scalar kind-column
scan before the timed write.

At source `8002d88`, on the locked 9700X with Clang 23.1.1, the complete
`test-executable`, `test-dynamic --cross-check` and `test-static --cross-check`
commands took 0.226, 1.540 and 3.171 s, respectively, on CPUs 0–7. All checks
passed, including 8 absolute-call refusals, 37 mapped-library replacement
checks and 6 failed-first-publication cleanup checks. These are whole-command
costs; queue waiting is excluded.

The final scalar direct-hit check and atomic publication policy measured as
follows for 180,000 workers, 185,452 atoms, 2,342,790 fixups and 63,181,112
output bytes. Each entry is the median of three separately locked process
medians (11 writes after warmup per process), with their min–max. One lane
uses CPU 2; eight lanes use CPUs 0–7. Times are milliseconds.

| Lanes | Publication | Count | Whole write |
| ---: | --- | ---: | ---: |
| 1 | Replace | 4.221 (4.213–4.251) | 21.651 (21.547–21.721) |
| 1 | New file | 4.209 (4.206–4.248) | 19.591 (19.519–19.725) |
| 8 | Replace | 1.498 (1.479–1.511) | 15.993 (15.395–16.131) |
| 8 | New file | 1.504 (1.504–1.509) | 13.149 (13.097–13.165) |

Both rows stage and rename. Replacing the benchmark's unreferenced old file
also releases its inode; the finish medians are 1.706 ms with one lane and
2.908 ms with eight. These
measurements describe the final writer, without a claim that the required
file-policy change is a throughput improvement.

The default shared-link benchmark also passed, including loading and running
each result. Its whole command took 43.058 s. The direct path uses CPU 2;
external linkers use their default threads on 15 online CPUs, or the explicit
one-thread CPU 2 configuration. Each size shares the binder snapshot/input.
Small cases use 41 linker runs, large cases 11 except GNU ld's 5. Times below
are milliseconds: direct total includes opening the binder input and writer
begin/end; external entries include the linker subprocess and produce
different ELF layouts and file sizes.

| Workers / filler | Direct total | Direct write | GNU ld 1 | lld default / 1 | mold default / 1 | wild default / 1 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 1 / 8 | 0.018 | 0.007 | 5.357 | 5.532 / 4.439 | 5.514 / 2.430 | 2.545 / 1.350 |
| 2,000 / 8 | 0.192 | 0.177 | 9.957 | 7.020 / 5.869 | 6.964 / 3.717 | 2.984 / 2.892 |
| 102,400 / 64 | 15.115 | 14.852 | 438.187 | 69.563 / 129.929 | 46.835 / 116.810 | 30.672 / 126.615 |

#### The unwind tables

Step 5 compares the direct path with linkers on outputs of the same features,
and a compiler's objects carry unwind tables, so the generated programs do too
(`link_program.c`). Each unit's `.eh_frame` atom holds GCC's CIE for x86-64
(`zR`, data alignment -8, the return address in register 16, FDE pointers
pc-relative `sdata4`) and an FDE per function of the unit: its `pc_begin` a
`PC32` fixup to the function, its range the function's length, and its call
frame instructions what the function's pushes and pops give, as GCC's CFI says
them. `_start`'s says that the return address is undefined, as glibc's does:
it is the outermost frame. The functions go unit by unit, as a compiler's
objects have them, so their addresses go in the order of their FDEs.

Both kinds of executable get `.eh_frame` and `.eh_frame_hdr` (step 4's static
ones too), with lld's layout: `.eh_frame_hdr`, then `.eh_frame`, after
`.rodata`, with `PT_GNU_EH_FRAME` over the header. `.eh_frame`'s atoms keep
link order, one after another, so that a terminator (`crtendS.o`'s) is only
ever last, where an unwinder that walks the records stops. The header is the
LSB's, what lld writes with `--eh-frame-hdr`: version 1, `.eh_frame`'s address
pc-relative, the count, and a table of `(initial location, FDE)` pairs from
the header's address, sorted by initial location, which unwinders search
(libgcc finds it through `dl_iterate_phdr`). An atom's records must be ones
the header can index: CIEs of a `z` augmentation with `R`, `P`, `L` or `S`,
FDE pointers pc-relative `sdata4` (GCC's and clang's for every x86-64 CIE),
and FDEs that point back to one of the atom's CIEs (eight at most, which a
compiler's object never reaches), each `pc_begin` a `PC32` fixup's field.
Other records fail the link (`EXECUTABLE_ERROR_UNWIND`, naming the atom), and
so does an initial location farther from the header than an int32 reaches,
which GNU ld and lld refuse too.

Three passes, each with an exact twin:

- **The count, before the layout**, in the count phase. The lanes share the
  `.eh_frame` atoms by cost, an atom's fixups plus one, each lane taking a
  run of them in id order as `lane_range_by_cost` cuts: a generated program's
  atoms are a cluster after its code, which ranges of atom ids gave to one
  lane. An atom's FDEs are read from its fixups, 16 at a time. Each fixup is
  a candidate FDE 8 bytes before its field, whose length and CIE pointer are
  one 8-byte load, made for all 16 before any is compared, where a walk's
  next record waits on the last one's length. Then compares: each candidate
  must start where the one before it ends (the first where the CIE does, the
  one before a group's first where the last group's last ended, a `valignd`
  carrying it in a register), point back to the CIE and be long enough, the
  fixups must all be `PC32`s of an addend in the column, and the last must end
  at the atom's end or its terminator's start. An atom laid out otherwise
  (other CIEs or fixups among its records, a CIE with a personality) goes to
  the exact walk, record by record, which is also what `exact` runs.
- **The table, once every symbol's reference is known**, in the dynamic
  phase (a static executable now has one too): each lane writes its share's
  entries, in `.eh_frame`'s order, from the lanes' counts before it. For an
  atom the vector path read, they come from its fixups' columns alone, 16 at
  a time: each initial location its target's reference plus its addend, each
  FDE its field's address less 8, both from the header, the first checked in
  64 bits against int32's range and the two interleaved by `vpermt2d`. An
  atom the walk read is walked again.
- **The sort**, on lane 0 before it opens the output: a check that the
  entries are sorted, 16 at a time (the initial locations are the even words
  of two vectors, a `vpermt2d` away, compared with themselves shifted by one,
  the last group's carried in a register). They are when the functions'
  addresses go in their FDEs' order, as in a link of a compiler's objects
  without cold parts (a static executable's libc has them: "The static
  executable with libc"). Only if they aren't does it sort them, by an LSD
  radix sort, stable, 8 bits a pass; the exact twin always sorts.

On the 9700X (`benchmark-executable --only links`), each program in place,
medians in µs, main's and then this one's (the median of three rounds,
taken alternately with main's in one session; on eight lanes the smallest
programs vary by more than they differ):

| Program (file) | 1 lane | 8 lanes |
| --- | ---: | ---: |
| static, 1 worker (3.9 → 4.6 KB) | 2.9 → 3.1 | 7.2 → 8.3 |
| static, 64 workers (16 → 21 KB) | 5.0 → 5.5 | 7.7 → 8.0 |
| static, 2,000 workers (397 → 510 KB) | 68 → 75 | 44 → 44 |
| static, 180,000 workers (35.5 → 45.6 MB) | 8,340 → 10,030 | 4,070 → 5,780 |
| PIE, 1 worker (6.6 → 7.2 KB) | 4.4 → 4.5 | 8.9 → 7.3 |
| PIE, 64 workers (19 → 23 KB) | 6.5 → 6.8 | 8.9 → 9.3 |
| PIE, 2,000 workers (401 → 513 KB) | 73 → 80 | 44 → 45 |
| PIE, 180,000 workers (35.6 → 45.7 MB) | 8,740 → 10,370 | 4,270 → 5,640 |

The largest static executable's phases, in place (the table is the dynamic
phase's, and the sorted check is in the open's):

| Lanes | count | layout | symbols | table | open | chunks | floor |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | 840 → 1,220 | 387 → 387 | 473 → 479 | 156 | 8.1 → 22.9 | 6,630 → 7,750 | 2,610 → 3,250 |
| 8 | 289 → 637 | 91 → 174 | 70 → 87 | 59 | 8.0 → 25.0 | 3,640 → 4,690 | 2,320 → 3,110 |

The passes on their own (`benchmark-executable`'s rows, one lane, over the
180,000 workers' 180,010 FDEs in 900 atoms, which stay in L3 from one run to
the next), per FDE: the count 0.70 ns by the vector path and 2.09 by the walk;
the entries 1.00 and 3.14; the sorted check 0.073, and the sort 3.40.

- **Most of the cost is the bytes.** `.eh_frame` and its header are 10.1 MB
  of the largest program's 45.6 MB, 56 bytes a function (48 of FDE and 8 of
  table entry), and writing them is most of what the link pays for them: the
  chunks take 1,120 µs more on one lane and 1,050 on eight, and the floor 640
  and 800 more. Per MB of output, one lane now writes the largest program in
  220 µs where main's took 235, and eight lanes in 127 where main's took 115.
- **The count reads `.eh_frame` from DRAM.** On one lane it takes 380 µs
  more: about 70 for the 180,010 more fixups that `executable_count` reads,
  and about 310 for the FDEs, 1.7 ns each where the benchmark's row, from
  L3, takes 0.70. An experiment that counted each atom's fixups instead,
  reading none of its bytes and checking nothing, took the count phase from
  1,226 to 913 µs on one lane and from 529 to 376 on eight, but the whole
  link only 1.3 to 2.2% less: the phases after it took longer by most of the
  difference (layout and symbols on one lane, the chunks on eight). So the
  checks stay in the count. Moving them to the chunks, where the bytes pass
  through the cache anyway, would save less than that, and a record the
  header can't index would then fail the link after the output is written.
- **What the measurements changed.** The first version's count split the
  `.eh_frame` atoms by id, as the other counts split theirs, and a generated
  program's come after its code, so on eight lanes the last lane counted all
  180,010 FDEs: 838 to 1,091 µs of count phase, which shares by cost made 516
  to 609. Its table checked each atom's records again while writing their
  entries, 1.7 to 2.5 ns per FDE: 467 to 503 µs on one lane, and 570 to 619
  on eight, which entries from the fixups' columns, in the count's shares,
  made 155 and 46 to 63. And
  the count loaded each candidate's length and CIE pointer separately and
  stored and reloaded the last group's end, as the sorted check did its last
  initial location: one 8-byte load per candidate and both carried in
  registers made the count 0.70 ns per FDE (from 1.0 to 1.8) and the sorted
  check 0.072 (from 0.287), 13 µs instead of 52 for the largest program.

#### The whole link

`benchmark-link` measures what "Summary"'s targets are about: the whole link
of a program that calls libc, by the direct path and by the linkers, on the
same program. The programs are generated as `benchmark-executable`'s are, but
without `SIZE32` and `SIZE64`, which wild 0.10 doesn't link: one worker, of
hello world's output size; 2,000 workers, of tcc's; and one of synth's shape,
its 102,400 functions in 512 units of 200, each with up to 64 filler
instructions, which come to 44 MB of output where synth's come to 46, with
1.33 million fixups where synth's objects have 1.02 million relocations.

- **The direct path** runs as a compiler runs it, on one lane: `runtime_open`
  from the snapshot and `bind_begin`; then, after the program's atoms (its
  code generation, outside the time), `bind_end`, `dynamic_inputs` and
  `executable_write`, over the output of the run before. Each part is the
  median of its runs, each from the link as it was before the part, and
  every run must write the same file (its build ID); the link is the parts'
  sum.
- **The linkers** link the object writer's objects of the same program with
  the appendix's command, the clang driver's with the matched options, timed
  as the appendix timed them, from `posix_spawn` to the wait's return, with
  their own threads (15 on the 9700X) and on one. Every output must run and
  print what the program's model predicts.

Medians on the 9700X in µs, the outputs on tmpfs over the file of the run
before (a second run gave the same within 4%), with the patch and the passes
over atoms and symbols that #212 made faster ("Patching and writing" and
"Layout"):

| | 1 worker | 2,000 workers | 102,400 workers |
| --- | ---: | ---: | ---: |
| **Direct path, 1 lane** | **14.6** | **75.0** | **6,650** |
| `runtime_open` | 9.3 | 9.2 | 11.8 |
| `bind_begin` | 0.5 | 2.1 | 86.8 |
| `bind_end` and `dynamic_inputs` | 0.4 | 1.0 | 37.2 |
| `executable_write` | 4.5 | 62.8 | 6,510 |
| Direct path, 8 lanes | 19.8 | 52.3 | 4,480 |
| One `pwrite` of the output | 0.7 | 11.9 | 2,990 |
| The objects (`-c`), 1 lane | 5.2 | 128 | 16,530 |
| GNU ld | 5,440 | 9,070 | 355,000 |
| lld, its threads / 1 | 5,860 / 4,800 | 6,870 / 5,860 | 55,800 / 98,000 |
| mold, its threads / 1 | 5,570 / 2,460 | 6,670 / 3,290 | 36,900 / 99,400 |
| wild, its threads / 1 | 2,580 / 1,350 | 2,910 / 2,500 | 25,200 / 99,900 |

Its work and theirs, for the program of synth's shape. The direct path's
page faults and memory are its warm-ups' (`getrusage`, and the growth of the
peak resident set); the linkers' are those of one run more without forking,
through a helper, with their own threads:

| | Bytes written | Page faults | Memory |
| --- | ---: | ---: | ---: |
| Direct path | 43,978,312 | 13 | 6.5 MB more resident |
| GNU ld | 49,248,600 | 38,045 | 171 MB peak resident |
| lld | 49,242,936 | 37,678 | 303 MB |
| mold | 53,737,480 | 17,002 | 317 MB |
| wild | 49,255,659 | 20,292 | 207 MB |

The direct path applies 1,332,827 fixups and looks up the program's 103,962
global names, each once in the snapshot's index; the linkers read 512 objects
of 96.2 MB, with 1,332,809 relocations and 673,314 symbols, besides the C
runtime's files, `libc.so.6`'s 3,170 definitions among them.

- **The linkers take what they took on the appendix's programs.** With
  their own threads, mold, wild and lld take within 5% of what they took on
  hello world, tcc and synth (GNU ld 10–14% less on the small ones). On one
  thread so do mold and lld, within 7%; wild takes 17% longer at tcc's size
  and 29% less at synth's, and on this program the three take within 1% of
  each other (`perf stat`: 1.16 to 1.66 billion instructions in 97 to 102 ms
  of task-clock).
- **Small links are the snapshot's.** One worker's link takes 14.6 µs, 9.3 of
  them `runtime_open`, of which 4.1 are its probes' `stat`s ("The runtime
  snapshot"), and 4.5 the write. More lanes cost a small link more than they
  save.
- **Large links are the write's.** The program of synth's shape spends
  6.51 ms of 6.65 writing, 5.31 of them on its chunks, patching 1.33 million
  fixups, hashing and writing 44 MB, where one `pwrite` of the output takes
  2.99. Eight lanes take 4.48 ms, 1.5 times that floor. Relaxing the loads
  and calls through the GOT in every kind added 0.26 ms to it on one lane,
  and 0.18 ms to the static executable's, for reading each relaxed call's
  opcode ("GOT relaxation in every executable").
- **The `-c` path pays before any linker starts.** Writing the objects takes
  16.5 ms on one lane, 2.5 times the direct path's whole link, into 2.2 times
  the output's bytes.
- **The outputs hold what each linker writes.** The linkers' have a symbol
  table, which the direct path doesn't write yet, and other sections besides:
  lld's output is 12% larger, mold's 22%. The direct path's link adds 6.5 MB
  to the compiler's resident set, where the linkers' processes peak at 171 to
  317 MB (76 MB for lld's link of one worker), their own code and libraries
  among it.
- **A program of `benchmark-executable`'s 180,000 workers** (45.7 MB, with
  2.34 million fixups: 1.8 times this program's, half of them kinds that the
  tests need and a compiled program rarely has, as "Patching and writing"
  says) takes 9.14 ms on one lane and 5.81 on eight, where it took 10.4 and
  5.8 before #212: one lane's count, layout and symbols went from 1.51, 0.39
  and 0.54 ms to 1.34, 0.30 and 0.31, and its chunks from 7.53 to 6.78, one
  `pwrite` of the output taking 3.05. GNU ld takes 825 ms on it, lld 114 and
  175 (its threads and one), mold 58.5 and 173, and wild 39.3 and 173.

How the counts are taken: a process spawned by the benchmark itself would
have the benchmark's peak resident set as its own, which Linux carries over to
the new program at `exec` from the memory it was spawned from. So the counted
run goes through a helper, the same executable run again, whose peak a program
that does nothing measures: 57 page faults and 7.3 MB, below every linker's.

**Static executables.** The same programs linked with `-static` (`--kinds`):
the direct path opens the static runtime's snapshot (`runtime_open_static`),
binds against it, and writes with `dynamic_static_inputs`; the linkers take
the appendix's `-static` command. Medians on the 9700X in µs, one lane:

| | 1 worker | 2,000 workers | 102,400 workers |
| --- | ---: | ---: | ---: |
| **Direct path, 1 lane** | **287** | **358** | **7,737** |
| `runtime_open_static` | 22.3 | 22.6 | 25.3 |
| `bind_begin` | 14.0 | 15.7 | 102 |
| `bind_end` and `dynamic_static_inputs` | 128 | 129 | 388 |
| `executable_write` | 123 | 191 | 7,222 |
| One `pwrite` of the output | 18.7 | 31.7 | 2,894 |
| GNU ld | 12,830 | 16,210 | 307,000 |
| lld, its threads / 1 | 8,690 / 6,620 | 9,470 / 8,180 | 53,800 / 98,200 |
| mold, its threads / 1 | 8,690 / 6,040 | 10,010 / 7,510 | 41,700 / 100,400 |
| wild, its threads / 1 | — | 4,040 / 5,050 | 25,900 / 103,300 |

- **A static hello world's link takes 0.29 ms,** where the appendix's C hello
  world took 3.92 ms with wild and 8.55 with mold. The output is 730 KB:
  `libc.a`'s 442 members that join, and the atoms' bytes binding copies.
- **Binding is most of it:** `bind_end` joins those members, checking each
  piece of the snapshot as it first reads it ("The runtime snapshot").
- **wild links no static executable of the 1-worker program.** wild 0.10
  starts a static executable's TLS segment at `.tdata`'s alignment past a
  multiple of the segment's when `.tbss` is more aligned, as this program's
  is (a 16-byte thread-local in `.tbss`). glibc's static startup takes the
  image to start on the alignment, and `malloc` then faults. A C program with
  a 16-aligned zero thread-local beside an 8-aligned one shows it, and GNU
  ld, lld and mold start the segment aligned. The benchmark doesn't run wild
  on such a program.

**Static PIEs.** The same programs linked with `-static-pie` (`--kinds
static-pie`): the direct path opens the static PIE's runtime's snapshot
(`runtime_open_static_pie`), binds against it, and writes with
`dynamic_static_pie_inputs`, its `ExecutableOptions.dynamic` and `.runtime`
both set; the linkers take the driver's `-static-pie` command (`-static -pie
--no-dynamic-linker -z text`, `rcrt1.o`, `crti.o` and `crtbeginS.o`, the
static executable's group, then `crtendS.o` and `crtn.o`). Medians on the
9700X in µs, one lane, with RELR ("The static PIE"), in one run with the
static executables (`--kinds static,static-pie`), whose figures came within
4% of the table above (the totals within 1.5%):

| | 1 worker | 2,000 workers | 102,400 workers |
| --- | ---: | ---: | ---: |
| **Direct path, 1 lane** | **298** | **379** | **7,894** |
| `runtime_open_static_pie` | 22.3 | 22.6 | 25.2 |
| `bind_begin` | 13.9 | 15.6 | 103 |
| `bind_end` and `dynamic_static_pie_inputs` | 128 | 132 | 379 |
| `executable_write` | 134 | 209 | 7,388 |
| One `pwrite` of the output | 19.1 | 31.9 | 2,911 |
| GNU ld | 13,090 | 17,020 | 336,600 |
| lld, its threads / 1 | 8,820 / 6,790 | 9,760 / 8,310 | 59,900 / 102,600 |
| mold, its threads / 1 | 8,670 / 6,220 | 10,040 / 7,630 | 42,500 / 106,000 |
| wild, its threads / 1 | — | 4,100 / 5,100 | 26,800 / 103,700 |

- **A static PIE's link costs a static executable's and a little more:** in
  the same run the static executables took 291, 362 and 7,664 µs, 2% to 5%
  less. Binding joins one member more ("Static links, as built"), and the
  write does a PIE's work besides the static runtime's. Against the linkers
  it is a static executable's link: 21 times faster than mold on one thread
  at hello world's size, 44 times faster than GNU ld, and 3.4 times faster
  than wild with its threads at synth's shape.
- **At synth's shape the write takes 0.23 ms longer** (7.39 ms against the
  static executable's 7.16), where the linkers' static PIEs cost them from
  1% less (mold with its threads) to 11% more (GNU ld) than their static
  executables. The count takes 0.82 ms against 0.72: it marks the static
  runtime's IFUNCs and the PIE's symbols, counts with both filters, finds the
  loads through the GOT that relax and classifies the ABS64 fixups. The
  dynamic phase takes 0.16 ms against 0.14, and the chunks 5.77 against 5.61.
  Since the static executable relaxes its loads through the GOT too ("GOT
  relaxation in every executable"), its chunks pay for reading its relaxed
  calls' opcodes as well, and the static PIE's write at synth's shape takes
  0.13 ms longer than its write where it took 0.27: 7.71 ms against 7.58.
- **RELR and the relaxed loads made the static PIE the smaller file:**
  44.15 MB against the static executable's 44.66. Its 105,077 relative
  relocations are 1,767 RELR words (14 KB), and its 22 IRELATIVE relocations
  stay in `.rela.dyn`. The linkers' static PIEs carry the relative ones in
  `.rela.dyn` with the driver's command (2.5 MB), and are 2.5 to 4.1 MB
  larger than their static executables. And the loads through the GOT of the
  image's symbols relax, so the GOT keeps 66 slots, where the static
  executable's kept 66,335 (531 KB) until it relaxed them too ("GOT relaxation
  in every executable"): it keeps 63 now, and its file is 44.13 MB, 23 KB less
  than the static PIE's.
- **wild's static PIEs have its static executables' flaw.** wild 0.10 starts
  a static PIE's TLS segment at `.tdata`'s alignment as it does a static
  executable's, and glibc's static-PIE startup then faults alike. Whether a
  link starts it off its alignment depends on the layout: on the 9700X,
  wild's static PIEs of the 1-worker program and of the C program above land
  aligned and run, while its static executables of both fault; on another
  machine, with another build of glibc 2.44, the C program's static PIE
  faults and its static executable runs. So the benchmark doesn't run wild on
  the static PIE of such a program either.

#### Synthetic sections

| Section | Contents |
| --- | --- |
| `.interp` | `/lib64/ld-linux-x86-64.so.2` |
| `.dynamic` | `DT_NEEDED` for `libc.so.6`, and for the loader when an import is its (`libc.so`'s `AS_NEEDED`), `DT_FLAGS`, `DT_FLAGS_1`, `DT_DEBUG`, `DT_INIT`/`DT_FINI` and the array entries, the table addresses and sizes, `DT_RELR`/`DT_RELRSZ`/`DT_RELRENT` or `DT_RELA`/`DT_RELACOUNT`, `DT_VERNEED`/`DT_VERSYM` |
| `.dynsym`, `.dynstr` | imports, and the program's definitions that a shared library in the link also defines or refers to (see "Exports") |
| `.gnu.hash` | those exported definitions, so the loader can find them (empty when there are none); every global definition with `-rdynamic` and in shared libraries |
| `.gnu.version`, `.gnu.version_r` | each import's default version from the snapshot, and the needed version names per library |
| `.got` | one slot per imported or weak-undefined symbol |
| `.rela.dyn` | a `GLOB_DAT` per import, an `R_X86_64_64` per pointer to one in data, and an `R_X86_64_RELATIVE` per pointer at an odd address |
| `.relr.dyn` | the other relative relocations, packed as RELR when libc defines `GLIBC_ABI_DT_RELR` (it does here, so the executable also needs that version) |
| `.eh_frame`, `.eh_frame_hdr` | a CIE and an FDE per function from the code generator, merged with the runtime's |
| `.symtab`, `.strtab`, `.shstrtab` and section headers | for debuggers and profilers, after the loaded segments |

`.eh_frame_hdr`'s table comes out sorted when functions are laid out in their
FDEs' order, as they are; the writer checks, and sorts it only if not ("The
unwind tables"). `.strtab` is the needed names copied from the name arena.

There is no `.note.gnu.property`. The output's property is the AND of its
inputs', and the program's code carries no IBT or shadow stack markers.

RELR is a list of addresses and 63-bit bitmaps of the 8-byte words after them.
The writer encodes it per atom, from each atom's first relocation, so that its
size is known before the layout ("The dynamic PIE"). Where
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

#### Streaming, as built

`streaming.c` is the experiment (#88): units of Tier 0's generated programs
(`tier0_generate.c`), compiled function by function into a static executable
laid out data first (the headers, `.data`, `.text`, then `.eh_frame_hdr` and
`.eh_frame` in a read-only segment of their own, then the DWARF), written while
they compile or once they have, and the same code as a Link through
`executable_write` in lld's order, for the direct path's time.

- **Every schedule writes the same file,** byte for byte with the same build
  ID: at the end; streamed by the compiling lane, each 256 KB chunk of `.text`
  hashed and written as soon as it is whole and none of its fields waits for a
  function not yet compiled; and streamed by a writer lane, to which the
  compiling lane hands its chunks.
- **The unwind tables** come from the code generator's prologues
  (`unwind_tier0.c`) as each function compiles, eight pairs at a time
  (`unwind.c`), and **the DWARF** (`debug.c`) in units of 256 functions, each
  made and finalized as soon as its functions have compiled and written after
  the units before it (`DebugOutput.bases`). In this order every address is
  known when its function is encoded, so nothing of either waits for layout but
  its place in the file, after `.text`, whose size the last function decides:
  both are written with the tail.
- **gdb, perf and llvm-dwarfdump read the data-first file**
  (`test-streaming --cross-check`): gdb stops in the program's functions and
  unwinds through them to `_start` by their FDEs, each frame at its function's
  line; perf's DWARF call graphs reach `_start` and give the lines;
  `llvm-dwarfdump --verify` passes the units together, and `readelf` warns about
  nothing.

**Measured on the 9700X** (`benchmark-streaming --units 3200`, medians of 11
runs, the output overwritten in place on tmpfs; [the record](https://github.com/buster14a/buster-performance/tree/1cae273d7d9484451744a8abe044ccddc3420d30/docs/measurements/streaming-2026-10-24)): 3,200
units of 32 functions, 102,400 functions of 13.1 million typed entries,
compiled by Tier 0 on one lane pinned to CPU 2 into 59.5 MB of code in an 86 MB
file, 6 MB of it DWARF:

| Variant | Whole | Compiling | After the last function |
| --- | ---: | ---: | ---: |
| At the end | 370.0 ms | 351.2 ms | 18.9 ms |
| Streamed | 370.0 ms | 365.1 ms | **5.0 ms** |
| Streamed, by a writer lane (CPUs 2 and 3) | 377.0 ms | 371.8 ms | 5.2 ms |
| Direct path (a Link, then `executable_write`), 1 lane | 390.6 ms | 349.6 ms | 41.2 ms |
| Direct path, 8 lanes (CPUs 0 to 7) | 382.9 ms | 350.2 ms | 32.8 ms |

- **Streaming takes the code's writing and the DWARF's making out of the end:**
  5.0 ms follow the last function against 18.9. The whole is the same on one
  lane, since the 14 ms of `.text`'s chunks and DWARF units move into the
  compiling, which they lengthen by as much.
- **What stays at the end is the tail:** 2.7 ms to copy `.eh_frame`'s pairs and
  the DWARF's units into it and lay out the rest, and 2.2 ms to write it. In
  this order both are made as the code compiles; only their place in the file
  waits for `.text`'s size.
- **A writer lane doesn't pay** on tmpfs: handing it the chunks costs the
  compiling lane more (371.8 ms) than writing them itself (365.1).
- **Against the direct path,** streaming saves building the Link (20.8 ms: the
  atoms, fixups and symbols of the same code) and most of the write (20.4):
  370.0 ms against 390.6 on one lane. On eight lanes, the direct path's write
  takes 12.1 ms after its Link's 20.7 on one: 32.8 ms after the last function.
- **Compiling callers first,** so that nearly every call is forward, the streamed
  file still leaves 5.1 ms after the last function (`--order callers`): a
  chunk's fields wait for callees that follow soon, within their unit.
- **The DWARF:** made at the end as one unit, it left 9.2 ms after the last
  function, against 3.0 without it; made in units as the functions compile,
  5.0. A line per function, 6 MB for 102,400 functions, measures the
  mechanism, not a `-g` build. Synth's `-g` output is 126 MB (mold links it in
  50.5 ms with its threads, wild in 41.4), most of it DWARF, whose making a
  compiler would spread over its compiling the same way. Its writing, at the
  floors' 8 to 14 GB/s on tmpfs ("The floors"), would stay at the end in this
  order, unless the DWARF went to a file of its own as its units are made (split
  DWARF).

### The object writer (`-c`)

The object writer serializes the same state as an ET_REL file that GNU ld,
lld, mold, wild and this linker accept, one object per unit (`atoms.unit`).
`src/buster/compiler/object_writer.c` implements it, and its orientation
comment has the rules below in full. Its structure is count, place, fill:

1. **Prepare,** once per link (`object_prepare`), for as many units as the
   caller says the link has, so that a translation unit without atoms still
   gets its object: counting sorts group the atoms by unit and class, and the
   symbols defined in atoms by unit, locals first. Each symbol gets what it is
   to its unit's object (local or global, in a string pool or not, and its
   place among them) in one 64-bit word, and each unit the bytes its names
   take.
2. **Count,** per object (`object_build`): the sections, with their sizes,
   alignments and relocations. Then one pass over the unit's fixups finds the
   symbols they refer to, the undefined ones among them, and the sections
   that need a section symbol. It notes what it found in a per-lane array
   indexed by symbol id, each entry stamped with the object's number, so the
   fill reads each relocation's symbol there instead of working it out again,
   and nothing is cleared between objects.
3. **Place:** section indices, symbol indices, string offsets and file ranges,
   with checked arithmetic.
4. **Fill:** each range is written once, into one buffer of exactly the
   object's size, and the gaps between ranges are zeroed. Nothing is
   allocated per symbol or relocation, and no buffer grows. Then one `pwrite`
   (`object_write_file`).

The design had each lane fill part of an object and one `pwritev` write the
parts. An object is a few to a few hundred microseconds of work, too little to
split, so lanes take whole objects instead (`object_write_units`): the units
are cut into parts by cost (their atoms' bytes, and a record's 24 bytes for
each fixup and symbol) and taken from the counter, as rule 1 has it. Each lane
builds an object in its scratch arena and writes it with one `pwrite`.

**Sections**, in this order:

- for each class with atoms, in class order: `.rodata`, `.rodata.str1.1`
  (`SHF_MERGE | SHF_STRINGS`), `.eh_frame` (`SHT_X86_64_UNWIND`), `.text`,
  `.init`, `.fini`, `.tdata`, `.tbss`, `.init_array`, `.fini_array`,
  `.data.rel.ro`, `.data` and `.bss`. With function sections,
  each `TEXT` atom has a section of its own, `.text.<name>`, after its first
  named symbol at offset 0. Each section is followed by its `.rela.<section>`
  if its atoms have fixups;
- `.note.GNU-stack`, which must be there or linkers assume an executable
  stack, and `.comment`;
- `.symtab`, `.symtab_shndx` when it is needed, `.strtab` and `.shstrtab`,
  where each `.rela.<section>`'s name ends with its section's.

In a section, each atom starts at the next multiple of its alignment, in id
order, with `int3` between code atoms and zeros elsewhere. A section's
contents start in the file at a multiple of its alignment, as GNU as places
them, up to 4 KB: linkers copy contents from any offset, and more would only
pad the file. The generated programs' `.eh_frame` atoms ("The unwind
tables") give each object a `.eh_frame` and its `.rela.eh_frame`. Not yet:
`.debug_*`, which waits for DWARF (step 9), and priorities in
`.init_array`'s and `.fini_array`'s names, which the representation doesn't
have.

**Symbols:**

- the null entry first;
- an `STT_FILE` with the source's name;
- section symbols for the sections relocations refer to;
- the unit's local symbols. Those without a name, string literals', are named
  `.LC0`, `.LC1` and so on, as GCC names them: wild 0.10 never finishes a link
  with a relocation against an unnamed local symbol in a string pool;
- then the unit's global and weak symbols, from `.symtab`'s `sh_info`, and the
  undefined symbols its fixups refer to, in the order they first do, with
  their binding and visibility and no type but `STT_TLS`, as GNU as writes
  them.

**Section indices:** past `SHN_LORESERVE` (65,280 sections, reachable with
function sections), symbols' indices go in `SHT_SYMTAB_SHNDX`, and `e_shnum`
and `e_shstrndx` in section 0's `sh_size` and `sh_link`. Internal ids never
inherit `st_shndx`'s 16 bits.

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

**Which symbol a relocation names.** Where GNU as and LLVM name a local
target's section symbol instead of the target, so does the object writer,
with the target's offset in its section added to the addend:

- a global, weak or undefined target is named itself;
- a local target is named through its section's symbol, except
  - for the GOT kinds (`CALL_GOT`, `LOAD_GOT` and `GOTPCREL`), the TLS kinds
    (`TPOFF32`, `GOTTPOFF`, `TLSGD`, `TLSLD` and `DTPOFF32`) and the size
    kinds, whose linkers need the symbol itself;
  - for an IFUNC, which linkers resolve through the symbol: a call or a
    pointer through its section's symbol would reach the resolver itself;
  - in a string pool, for a PC-relative kind or any addend. Linkers split a
    pool into its strings and send a reference through the section's symbol
    to the string its offset and addend land in, which is the target's only
    for an absolute reference without an addend: `lea rax, [rip + .LC0]`'s
    addend is −4, which would land in the string before.

An object that defines an IFUNC is `ELFOSABI_GNU`, as GNU as and LLVM mark
it, and the others `ELFOSABI_SYSV`. An absolute symbol is in no unit, so no
object could define it as the link does: `object_prepare` refuses a link
that has one, rather than write objects that leave it undefined. The
program's units have none; only imported objects (the static runtime's) do,
and no object is written for them.

The scalar kernels decide the per-record costs. Writing `Elf64_Rela` from
columns takes 0.18 ns per record in L1 and 0.61 ns from DRAM, and the AVX-512
transpose of three qword columns into 24-byte records (three `vpermt2q` and
three masked permutes per 8 records) takes 0.17 and 0.62 ns. `Elf64_Sym` has
the same shape. Both are store-bound, so they stay scalar.

**What an object costs** (`benchmark-object-writer` on the 9700X: one lane on
CPU 2, medians of 21 passes, per object). The programs come from
`link_program_generate`, with 8 globals, 2 thread-locals, 20 strings and an
init and a fini function per unit:

- **synth-like:** 64 units of 200 workers of up to 40 filler instructions,
  each about one of synth's translation units: 54 KB of code, 2,411
  relocations, 1,208 symbols and 17 sections, 151 KB written;
- the same with function sections: 419 sections, 182 KB written;
- **one large unit** of 12,800 workers: 3.4 MB of code, 153,649 relocations
  and 12,854 symbols, 7.6 MB written;
- **small:** 64 units of 4 workers of up to 4 filler instructions: 0.7 KB of
  code, 59 relocations and 80 symbols, 6.1 KB written.

| Objects | File system | Build (µs) | In place (floor) | New file (floor) | In all: in place, new (× the floor) |
| --- | --- | ---: | ---: | ---: | ---: |
| synth-like | tmpfs | 14.5 | 8.5 (8.4) | 20.0 (19.7) | 25.1 (3.0×), 36.7 (1.9×) |
| | btrfs | 14.5 | 24.2 (24.0) | 35.2 (34.5) | 41.0 (1.7×), 51.9 (1.5×) |
| synth-like, function sections | tmpfs | 18.2 | 10.1 (10.0) | 24.2 (23.6) | 30.8 (3.1×), 44.9 (1.9×) |
| one large unit | tmpfs | 605 | 435 (432) | 926 (917) | 1,004 (2.3×), 1,495 (1.6×) |
| | btrfs | 596 | 1,107 (1,110) | 1,341 (1,342) | 1,665 (1.5×), 1,899 (1.4×) |
| small | tmpfs | 0.59 | 0.85 (0.74) | 3.14 (3.07) | 1.58 (2.1×), 3.86 (1.3×) |
| | btrfs | 0.60 | 1.84 (1.68) | 9.07 (9.18) | 2.60 (1.5×), 9.84 (1.1×) |

- **Build** is `object_build` alone, one object after another: 10.7 GB/s and
  6.0 ns per relocation for the synth-like objects, 12.9 GB/s and 3.9 ns for
  the large one. `object_prepare` adds 1.4 µs per object (92 µs once for the
  64 units).
- **In place** overwrites the object's file from the pass before, and **new
  file** unlinks it first. Their floors are the same system calls without the
  writer's `fstat`.
- **In all** is the build just before the write, as a compile does it (2 µs
  more for the synth-like objects than one after another, since the writes
  push the representation out of the caches), and the write.

So an object costs 1.1–3.1× its file. The writer is within 0.7 µs of the
floor (within 1% for the large object), and the build is what is left.

**Where the build's time goes,** for the synth-like objects: 80,000 cycles and
238,000 instructions per object (IPC 3.0).

| Phase | Cycles | Instructions |
| --- | ---: | ---: |
| Count sections | 2,500 | 7,300 |
| Count symbols: the pass over the fixups | 25,500 | 69,400 |
| Place | 500 | 1,100 |
| Fill sections: the atoms' bytes and the gaps | 9,300 | 13,900 |
| Fill relocations | 17,900 | 86,900 |
| Fill symbols and their names | 21,700 | 58,500 |
| The rest: the buffer, its gaps and the headers | 700 | 1,000 |

The estimate here was a few microseconds per object, from the record writers
alone. The records cost 7 cycles per relocation and 18 per symbol instead,
since each gathers its fields from several columns at random rather than
streaming three of them. Counting the symbols mispredicts about 1,100 branches
per object, mostly at the first meeting with each symbol. A version without
that branch, which loads and stores every time, was a third slower, since the
count of undefined symbols became a chain through the loop. What paid:

- the per-lane array instead of a hash table of the symbols met: 19.7 µs per
  object to 16.7;
- the columns' pointers in locals (with `-fno-strict-aliasing`, every store
  makes the compiler load them again otherwise), and each name of up to 64
  bytes copied with one masked load and store: 16.7 µs to 14.4.

Copying the atoms with AVX-512 instead of `memcpy` didn't.

**Writing the file** (`object_write_file`):

- One `open`, with `O_CREAT` and never `O_TRUNC`, then `fstat`. A regular
  file with one link, or the empty one `open` just made, is written in place
  with `pwrite`, and truncated only if it was longer.
- A file with other hard links gets a new file beside it, in the same
  directory under a name of its own (`mkstemp`, which a suffix to the
  destination's name could take past `NAME_MAX`), with its mode, renamed over
  it, so the other links keep the old contents.
- Anything else (`/dev/null`, a pipe) is written as it is, in order: `write`,
  since a pipe refuses `pwrite`.
- A group of lanes that can't start, because a lane has no room for its
  order of parts or its symbols, writes no object and gives every unit
  `OBJECT_ERROR_FULL`.

For a 155 KB file, each way writing files of its own (µs):

| Way | tmpfs | btrfs |
| --- | ---: | ---: |
| `open`, `pwrite` over a file of the same size, `close` | 9.7 | 24.6 |
| the same with `ftruncate` to the same size | 9.8 | 25.4 |
| the same with `fstat` first, with or without `ftruncate` | 9.2–9.3 | 24.2–25.4 |
| `open` with `O_TRUNC`, `pwrite`, `close` | 18.9 | 47.6 |
| `unlink`, `open` with `O_CREAT \| O_EXCL`, `pwrite`, `close` | 21.5 | 38.7 |

In place is 1.6–2.2× as fast as a new file, and `O_TRUNC` throws that away.
`ftruncate` to the same size costs about 1 µs on btrfs, so a rebuild's object,
which keeps its size or grows, skips it.

The order of the writes matters as much as the way. A write costs less when
the pages it writes are still in the CPU's caches: when the file was just
written, or when a new file gets the pages an unlinked one just gave back.
That is why the floors here are above those in "The floors", which rewrote one
file over and over: on tmpfs, 8.4 µs for 151 KB here against 3.9 µs right
after another write of the same file. So the benchmark gives each way files of
its own, which it last wrote a pass before (39 MB of other writes earlier for
the synth-like objects), and lets the ways take turns going first. A first
version, whose floor rewrote the file the writer had just written, put the
writer 1 µs behind its floor on tmpfs and 12 µs on btrfs.

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

#### Explicit executable-stack requests in foreign objects

The executable writer emits a non-executable `PT_GNU_STACK`. Foreign
conversion therefore refuses a selected ET_REL object whose exact
`.note.GNU-stack` section has `SHF_EXECINSTR`; it does not silently discard
the request or enable an executable stack for the whole output.

The existing scalar and vector section-import passes preserve the first
requesting section index in `ImportObject.executable_stack_section`, including
non-ALLOC notes whose bytes do not become atoms. Conversion checks that
metadata before importing the object's local symbols. The refusal is
`FOREIGN_LINK_ERROR_IMPORT` with `IMPORT_ERROR_UNSUPPORTED`, identifying the
original object, section, section-header offset and type. Scalar retry clears
the metadata before rebuilding it.

Selection is at the object boundary. Direct inputs and extracted regular or
thin archive members contribute; a member that was never extracted does not.
Forced and whole-archive extraction still selects the requesting object.
Discarding its code, including all of its COMDAT code, does not erase its
stack request. This bounded policy also conservatively refuses an executable
marker that itself has `SHF_EXCLUDE` or belongs to a losing COMDAT group;
that policy is not a claim of identical GNU/lld/mold behavior for unusual
markers. Missing notes and notes without `SHF_EXECINSTR` retain the existing
behavior. DSO `PT_GNU_STACK`, automatic executable-stack support and other
importer consumers are outside this change.

`test-import` checks exact names, flags, positions across vector batches and
tails, first-request selection, reused output objects and failed-vector scalar
retry. `test-foreign` assembles original direct/archive/COMDAT cases and checks
selection and the original diagnostics through the exact/vector paths at
1/2/3/8 lanes, including the dynamic conversion entrypoint. These are producer
and section-import checks; they do not claim an executable runtime, GC
composition or external-linker oracle for the refusal policy.

#### Foreign COMDAT unwind pruning

`foreign_unwind.c` filters an object's `.eh_frame` while the original
section ownership is still available. A unique `R_X86_64_PC32` relocation
at the FDE's initial-address field identifies its original code section.
The selected section-to-atom map decides whether that FDE survives. A
canonical symbol with the same name in a winning COMDAT group cannot keep
the losing group's FDE alive. The pass also drops CIEs no retained FDE uses;
deduplicating compatible live CIEs is a separate pass.

The supported input is DWARF32 CIE version 1 or 3 with `zR`/`zPLR` augmentation
and the optional signal-frame marker, and PC-relative signed four-byte FDE
addresses. Personalities and LSDAs can use fixed absolute four/eight-byte
or PC-relative four-byte pointers, including indirect pointers. Their
relocations follow the owning record; they do not decide code liveness.
Unsupported encodings, missing or ambiguous ownership, overlapping fields
and malformed records are refused before the executable is written.

The producer returns fresh compact bytes, relocation records and an
original-to-compact interval map. It rewrites CIE backlinks and relocation
offsets, preserves retained CFI and augmentation bytes, and leaves the input
unchanged. A failed prune restores the output arena and leaves the result
untouched. An all-discarded section stays empty; the pass does not invent a
terminator. The original section endpoint has an explicit mapping.

`foreign_unwind_link.c` applies this only to selected objects with discarded
COMDAT sections, before canonical names and relocation requirements are
finished. It remaps retained symbols and their extents. A section symbol at
zero names the compact section even if its first original record was dropped.
References with nonzero addends into a changed unwind fragment are currently
refused because their address-expression bias is not retained. Local labels
in discarded records become inert slots; any surviving use is rejected.
The ordinary relocation importer consumes the compact records through
`import_relocation_entries`; its existing section-table wrappers retain their
contract. Mapped foreign inputs and the conversion arena outlive writing.

`test-foreign --cross-check` exercises original records, malformed fields,
relocation tails, rollback, symbol endpoints and COMDAT ownership through the
exact and vector paths. Its C++ fixture compiles two original translation
units with a duplicated throwing template. The losing object retains another
function; throwing through that function must run both cleanup destructors
and reach the caller's catch. GNU ld and lld link and run the same inputs.
The explicit runtime line includes `-lstdc++ -lm`, as the observed system C++
driver does. Finding libraries named only in another DSO's `DT_NEEDED` is
outside this unwind test's contract.

`benchmark-foreign-unwind [--runs N] [--fdes N] [--profile mixed|all|none]`
measures the complete prune call on original synthetic `zR`/`zPLR` records.
Defaults are 21 paired samples after two warm-up pairs, 33/4,097/65,537
FDEs, and mixed/all/no retained-code profiles. An independent compact byte,
relocation and interval-map oracle validates both paths before timing; every
sample is checked afterward and the frozen input must remain identical.
Both paths share variable-record parsing and ownership lookup. The vector
path batches relocation classification and transposes retained output records.

Each measurement process runs under the shared 9700X lock on CPU 2. Timing
includes validation, sorting, scratch/output allocation, copying and relocation
transposition. Arena resets, verification and statistics are outside the clock;
the reusable arenas retain committed pages. Reported arena lengths are logical
bytes, and the labelled temporary-payload estimate excludes alignment, VM
reservations and RSS. These are synthetic phase measurements, not complete
foreign-link throughput, corpus acceptance or evidence that the wider linker
performance target has been reached.

#### Foreign section garbage collection

`foreign_gc_mark` traverses an unlaid-out `Link` and returns stable maps for
its live atoms, symbols and fixups. The caller supplies the entry, exported
or forced symbols and explicitly retained atoms. Init/fini fragments and
arrays are implicit roots. Defined symbols survive with their atoms;
atomless symbols survive only when rooted or referenced by a live fixup.
The pass does not modify the input or its borrowed bytes. Map entry zero
and every discarded entry are zero; retained IDs follow original order.

Root classification, frontier seeding and map emission have scalar and
AVX-512 paths. Two atom queues and mark-before-append bound the irregular
walk to one visit per atom. Target lookup and live-bit updates remain
scalar. This is a single-lane closure, not the planned per-lane frontier
exchange; representative scaling and complete-link measurements remain
separate acceptance work.

`foreign_gc_link` adds the foreign producer's ownership rules. It creates
temporary graph nodes for FDEs and CIEs, with code-to-FDE and FDE-to-CIE
dependencies. Personality and LSDA relocations then participate in the same
closure as ordinary references. An explicit reference to an entire unwind
atom conservatively retains all its records. Merely retaining a CIE does
not retain every FDE that uses it. The adapter also adds dynamic exports,
init/fini entry points, command-line `-u` names and `SHF_GNU_RETAIN` sections
to the roots.

After marking, the adapter replays the original unwind records with dead
code sections removed and composes those offsets with prior COMDAT
pruning. It builds a fresh compact `ForeignLink`, copying retained section
bytes and the name table, and remaps the original object section/symbol
maps, resolved names and dynamic imports/exports. Library/version strings
and original foreign input metadata remain borrowed: the `Foreign`, input
conversion and both output arenas must outlive writing. Input and output
must be distinct. Failure restores both arena lengths and leaves the
output unchanged.

Use `foreign_link_build_for_gc` when dead strong references need deferred
diagnostics. That explicit producer keeps unresolved references until
reachability determines whether they survive; validated ordinary unlaid-out
conversion is also accepted, but reports those references eagerly. The final compact
link rejects surviving strong undefined references. Unsupported unwind
formats, references into discarded unwind records and nonzero addends
into changed unwind fragments are refused. Collection runs before terminal
string or CIE pooling; a pooled terminal link is not an input to this pass.

`test-foreign --cross-check` compares both marker paths with an independent
reachability model over chains, branches, cycles, duplicate targets,
guarded tails and failure cases. Foreign adapter fixtures test ordinary
and unwind closure, COMDAT composition, forced/retained roots, external
map remapping, rollback, dead versus surviving undefined references, and
written executable behavior against GNU ld and lld. The adapter is opt-in:
`link-foreign --gc-sections` runs it ([the command](foreign-driver.md)).

`benchmark-foreign-gc --counts 8,1024 --runs 21` creates original static
ET_REL leaf objects with one CIE/FDE each, then compares GC on/off with the
exact and vector paths. The mixed-dead entry calls alternating leaves; the
all-live control calls every leaf. An untimed replay checks original ELF
identities, stable maps, every retained unwind offset and serialized FDE
addresses/ranges. Every timed output executes and matches complete reference
bytes outside timing. The all-live byte reference is shared across GC on/off
as well as both paths; the mixed-dead references are separate by GC state.

Each sample reserves fresh link/name arenas, opens/resolves the frozen
objects, converts, optionally collects, writes/closes and releases the
per-link arenas and mappings before its total clock stops. Generated files,
commands and thread-local scratch caches persist. Generation, output unlink,
optional scalar conditioning and validation are excluded. This measures
warm-file fresh links, not cold I/O or compilation. Phase medians need not
sum to the total median. The report includes wall min/median/max, reference
TSC ticks, input/output bytes and rates, logical code/CFI bytes, live counts
and retained arena lengths. Those lengths exclude mapped input files,
generated fixtures and transient scratch; they are not peak RSS.

Variants rotate by default. `--order` selects their order, `--schedule blocked`
groups samples, `--only mixed|all` selects a control, and `--after-scalar MS`
adds excluded conditioning. Run each independently started process under the
shared 9700X lock on CPU 2, alternating orders and comparing process medians
against spread before making performance claims. This single-lane static
fixture supplies no dynamic, COMDAT, personality/LSDA, corpus or scaling
performance evidence.

The isolated `4cb4936aaef2b980e4b2f29101cf8e86879f8336` source built and
passed `test-foreign --cross-check` on the shared 9700X: marker 36,694 checks,
adapter 281, C++ unwind 72, with no failures in those or the earlier groups.
Three independently started benchmark processes used the command above,
then reversed variant order, then blocked order. Every map/CFI/runtime/byte
oracle passed. The table gives the range of process medians in microseconds,
not a confidence interval or the within-process min/max:

| 1,024-leaf case/path | GC off | GC on | Output bytes off → on | Retained arena bytes off → on |
| --- | ---: | ---: | ---: | ---: |
| Mixed-dead, exact | 3,371–3,447 | 2,720–2,908 | 91,040 → 50,080 | 1,830,055 → 2,286,572 |
| Mixed-dead, vector | 3,523–3,616 | 2,849–2,903 | 91,040 → 50,080 | 1,862,855 → 2,319,372 |
| All-live, exact | 3,501–3,568 | 2,906–2,971 | 95,136 → 95,136 | 1,814,103 → 2,393,500 |
| All-live, vector | 3,560–3,640 | 2,992–3,090 | 95,136 → 95,136 | 1,846,903 → 2,426,300 |

The large fixture's reduction exceeds the between-process median spread.
The eight-leaf cases are near 55 microseconds and do not establish a gain
beyond observed spread. The all-live control also improves despite removing
nothing: the adapter's copying changes byte placement and memory access, so
these results do not isolate a reachability or SIMD speedup. Page-fault/cache
attribution remains unmeasured. Retained arena growth is real; peak process
memory was not measured. Default activation, corpus coverage and scaling
remain separate decisions.

Complete original build/test/benchmark logs and the frozen source manifest
are retained in [the GC measurement directory](https://github.com/buster14a/buster-performance/tree/85fbcec02dafec90652bc56c24513e2622c06f61/docs/measurements/foreign-gc-2026-10-23).

#### Foreign terminal CIE pooling

`foreign_unwind_dedup_plan` considers EH atoms in their emitted order after
COMDAT pruning and optional collection. Consecutive eligible fragments can
share one byte-identical CIE only when its canonical fixup targets, kinds,
relative offsets and full addends also match. A pool contains at most one
distinct CIE, preserving the executable writer's single-CIE vector path.
FDE order and the relative order of non-EH atoms remain unchanged. Variable
record traversal and ordered pool formation are shared; bounded byte/fixup
comparisons have exact and vector paths.

Terminators, explicit/automatic roots, observable named anchors, references
into unwind bytes and incompatible CIEs prevent unsafe pooling. A successful
no-savings plan remains valid. An aliased CIE is distinct from a discarded
record: its offset still maps to the retained CIE. Every original section
has an explicit mapped endpoint, so a one-past symbol does not acquire the
endpoint of a later fragment in the same pool. Pool alignment preserves the
maximum input section alignment. Individual translated record positions
preserve the writer's effective fragment requirement, `min(alignment, 4)`:
the existing executable writer places EH fragments without inter-fragment
padding. Tests include mixed 4-, 8- and 16-byte section alignments and
multiple identical CIE records within one atom.

`foreign_unwind_dedup_link` returns a distinct wrapper with fresh mutable
Link columns, bytes, object maps, root/export arrays and historical unwind
record maps. Symbol and name IDs remain stable. The wrapper's section maps
are authoritative for original-ELF-to-pooled byte offsets; historical unwind
plans retain pre-pooling coordinates, with remapped code-owner atom IDs.
Immutable library metadata and historical unwind bytes remain borrowed, so
the `Foreign`, input and both output arenas must outlive the result. Input
and output are distinct. Any failure rolls arena lengths back and preserves
both caller objects. `UNSUPPORTED` permits an unchanged-input fallback; it
does not authorize ignoring malformed input or memory failures.

The output is terminal: it may go to compatible string pooling and the
executable writer, never back to GC, original relocation import or ET_REL
writing. Shared-object output is outside this initial contract. A pooled
wrapper documents this caller contract; its embedded `Link` is not a runtime
tag that prevents misuse by an unrelated consumer. A pooled
EH atom uses unit zero; only an EH pool as the source of a reference may
refer to its constituent units' locals. A local target inside an EH pool
does not gain a cross-unit exception. The adapter copies the whole link
for transactional publication, including successful no-savings cases; that
cost is part of acceptance rather than assumed negligible.

`test-foreign --cross-check` checks plans, capacities, rollback, exact/vector
agreement, record and endpoint mappings, fixup signatures, pins and alignment.
The integrated fixture composes losing COMDAT pruning, GC and CIE pooling,
then runs an original C++ throw/catch/destructor program with personality
and LSDA records. That runtime gate is separate from the synthetic timing
fixture and does not establish general C++ corpus coverage.

`benchmark-foreign-unwind-dedup --counts 8,1024 --runs 21` compares exact and
vector whole links with pooling on/off. Its original static leaf objects
have either identical CIEs or different code-alignment factors unused by
their instruction-free FDEs. An independent original-section/FDE map replay,
runtime checksum and full output-byte comparison run outside timing. Unique
CIE outputs must match across pooling states as well as paths. Timed work
includes fresh arenas, input mapping/resolution, conversion, optional
pooling, writer close and cleanup. Fixture/command generation, output unlink,
conditioning and validation are excluded; input files and scratch caches
are warm. Retained arena lengths exclude mappings and transient scratch and
are not peak RSS. Wall spread, reference TSC ticks, removed records/bytes
and output size accompany phase and total medians.

Run independently started repetitions under the shared 9700X lock on CPU 2,
varying `--order`/`--schedule` and optionally `--after-scalar`. Phase medians
need not add to the total median. This original single-lane static fixture
supplies no dynamic, COMDAT, personality/LSDA, corpus or scaling performance
claim. The isolated slice passed its build, full foreign cross-check and
call-graph gate on the locked 9700X, including 812 pooling checks, 136
COMDAT/GC/CIE composition checks and 72 C++ unwind runtime checks. Three
independently started CPU-2 benchmark processes each passed 1,118 pipeline
checks. The [retained evidence and precise process-median tables](https://github.com/buster14a/buster-performance/blob/85fbcec02dafec90652bc56c24513e2622c06f61/docs/measurements/foreign-cie-2026-10-23/README.md)
identify the frozen source/binary and timing boundaries; host input identity
receipts remain pending.

At 1,024 leaves, duplicate-CIE whole-link medians fall by 18.13% on the exact
path and 19.14% on the vector path, while retained arena bytes increase.
The unique-CIE control also gets faster with no records or bytes removed:
the adapter copies the link and changes memory placement, so these figures
do not establish a CIE-only or SIMD-only cause. Peak RSS was not measured;
the eight-leaf changes remain limited observations at the recorded spread.
These isolated runs use the newer writer's additional 4,096-byte file gap
and are not mixed with earlier composed-snapshot timings. Performance
qualification remains pending: all three processes used `--after-scalar 0`;
bounded repeated `--after-scalar 100` processes and review of the exact
build's emitted assembly remain before final SIMD-state acceptance. Broader
corpus and scaling acceptance remain open. Pooling stays opt-in.

#### Steps 1 to 5, as built

`foreign.c` (the first part of step 7) resolves a command line on a group of
lanes and leaves what the later steps read: each loaded object's section
table, symbol table, COMDAT groups and relocation sections, each global
name's winner, and each group's fate. `foreign_arguments` takes paths, `-l`
(and `-l:FILE`), `-L`, `-u`, `--whole-archive`, `--as-needed`, `-Bstatic`
and `-Bdynamic`, and groups, which the rounds make moot. Nothing is written
yet: garbage collection, the relocation scan, merged strings and the write
are the later parts.

- **Positions.** Every input has a place in link order, a script's inputs at
  the script's place, and an archive's member is at its archive's place in
  member order: `(input + 1) << 32 | member`. Every comparison of definitions
  and every decision to load a member goes by position, so the result doesn't
  depend on which lane or which round met a record first.
- **Open** (step 1). The inputs are found (`-l` as GNU ld searches, a
  script's inputs as lld finds them) and mapped privately, split across the
  lanes by count, each opened without blocking, so that a FIFO is refused
  rather than waited on; lane 0 reads the scripts and puts their inputs in
  their place. Then each archive's symbol index alone (`import_archive_lazy`:
  the index and the headers of the members it names, no member's bytes; a
  thin archive's member is a file of its own, mapped when it loads, or, where
  GNU ar added a regular archive to a thin one, a member of that archive,
  named `/N:M` and read from its header there), its names hashed eight at a
  time and indexed as a library's exports are (`import_index_build`), with
  each entry's name, length and member copied into the index's order, so that
  a probe reads its bucket's entries one after another; and each library's
  exports (`import_library`).
- **Rounds** (steps 2 to 4). The first round's objects are the command
  line's, with an object of references for each shared library not under
  `--as-needed`: its undefined symbols, weak or not, at its place, which load
  the members that define them as an object's references do (as GNU ld and
  lld load them). A library that refers to no name, such as the loader,
  takes no object: it becomes needed without a round of its own. Each round
  parses its objects, exchanges their records, resolves them and decides
  which members load, which are the next round's objects, until a round
  loads none. A round's columns are pushed once its headers have counted
  them, so the link touches no more memory than it uses.
  - *The parse:* each object's header, section table, COMDAT groups and
    relocation sections, by object, eight section headers at a time on the
    vector path (transposed by `link_transpose_words`); then the global
    symbols, by symbol across objects, in a count, place and fill over the
    round (rule 3): eight entries at a time, their classes, visibilities and
    sections as masks (`SHN_ABS` and `SHN_COMMON` kept out of the range of
    the sections `SHN_XINDEX` reaches, which number them too past 65,280
    sections), their names' lengths by masked loads, their hashes by
    `link_hash_rows`, and a copy of each name, which the resolution compares,
    made by a masked store of the row the hash read.
  - *The partitions* come from round 0's counted records and the archives'
    index entries, about 2,048 records each: an input's size, never the
    lanes. The file sizes they were first taken from gave the synth-size link
    2,048 partitions of 200 records; from 128 to 2,048 of them the resolution
    took the same time within 5% on the 9700X, so the count only spreads the
    work over the lanes.
  - *The exchange* (rule 4): a 32-byte record per global symbol and per
    group: the hash, the name and its length, the symbol, a tag (class,
    visibility and object) and the section. The design's 16 bytes (a hash,
    the symbol and its class) left a partition's lane to read the name, its
    length and the section from the round's columns, a miss each at a
    symbol's place. The counts are a histogram of the hashes' top bits, the
    places an exclusive scan over partitions and lanes, and the records are
    built eight at a time by a transpose and stored from the registers, two
    per vector: a batch stored to memory and copied a record at a time read
    each record back from a store twice its size, and lost to the exact path.
  - *The resolution:* each lane takes its partitions, whose tables (names and
    group signatures, by open addressing) it keeps from round to round, and
    probes them once per record, on both paths. A probe is a few loads in the
    partition's table; what a record costs is the compare of its name with
    its entry's, which waits for both names' bytes. So each record's name is
    fetched 16 records ahead, and in a partition whose table held names
    before the round, its hash's first slot, the entry it holds and that
    entry's name in three stages, 24, 16 and 8 records ahead. A later round's
    records find an earlier round's entries, cold by then; a first round's
    find the entries its own records just made, still in cache, where the
    stages only cost: in a run of each way on the 9700X, the synth-size link's
    resolution took 5.8 ms with the stages and 4.9 with the names alone (5.0
    without any prefetch), and its members kind, whose second round resolves
    310,000 records against the first's entries, 9.5 and 10.4.
  - *Loading:* each name the round touched decides (`foreign_decide`). Its
    first member by position, found by probing once the indices of the
    archives before the name's earliest definition when a strong reference
    comes before it, loads when both the member and the earliest strong
    reference come before the earliest definition, and before the first
    shared library that exports the name if every symbol of it has default
    visibility. A later definition doesn't stop it: for `start.o libfoo.a
    weak.o`, GNU ld loads the member, whose strong definition wins, and with
    a strong definition in place of the weak one the two are a duplicate.
    Only the libraries before that bound can shadow the member, so only they
    are looked in; the winners look in the rest for a name left without a
    definition. Then GNU ld's common rule: a common symbol without a strong
    definition loads the first member after it that defines the name as
    global data, read from the member's symbol table without loading it. And
    a library under `--as-needed` becomes needed when a strong reference with
    default visibility finds a name it exports still undefined before it, as
    GNU ld decides it left to right (`foreign_needed_library`). Lane 0 marks
    the needs, and the members not loaded before and the newly needed
    libraries' objects of references are the next round's objects, in
    position order.
- **COMDAT groups** (step 5). A group's record goes through the exchange with
  the symbols', and its partition's group table keeps each signature's
  earliest group by position. After the last round each object's sections in
  a discarded group are marked, and its definitions in them become references
  of their binding, as GNU ld and lld make them.
- **The winners.** Each name's strong definitions (two or more are a
  duplicate), its largest common symbol (with the largest alignment), its
  earliest weak definition, its shared library's export, or nothing: a strong
  reference to nothing is an error the link reports unless garbage collection
  drops every reference, a weak one is 0. Then each symbol's name id.
- **Failures.** After each phase every lane decides whether to go on
  (`foreign_proceed`), and all must decide alike: a lane that skipped a phase
  another took would leave that one waiting at the phase's syncs. A failure
  carries the number of decisions its lane had taken, and a decision counts
  only the failures from before it, which the sync before it shows every
  lane; a failure in the next phase, of a lane that has gone on, waits for
  the next decision. A group that `lane_run` couldn't start, or an operation
  its command-owned team couldn't accept, fails as `FOREIGN_ERROR_LANES`.

The members loaded and each name's winner are GNU ld's, except where GNU ld's
result depends on the order of the archives on the command line; there they
are lld's and mold's. `test-foreign --cross-check` compares them with GNU
ld's `--trace`, `-y` and map, and with lld's `--why-extract`, `-y` and map.
Where the three linkers differ:

- **an archive before the input that needs it** (GNU ld loads nothing for a
  reference after its archive), and a shared library's export of a name that
  an archive before it defines: lld's;
- **GNU ld's common rule**, which lld doesn't have: GNU ld's;
- **ties between members of one archive** (two weak definitions, commons of
  one size, COMDAT groups of one signature) go by member order, as mold's
  do: GNU ld and lld order a member loaded for a member after it after that
  one;
- **loading only adds:** a member a round loads stays, where lld, which loads
  in input order, can find a definition first that a later round of this walk
  finds;
- **a library under `--as-needed` that nothing needs:** GNU ld's, which drops
  it, so that its own references load nothing; lld loads the members they
  name;
- **a thin archive of another archive's members** (GNU ar's `/N:M`): GNU
  ld's, which reads them as this path does; lld 23 refuses the archive.

The tests count each documented difference and print it on a line of its own
rather than as a failure.

Measured on the 9700X (`benchmark-foreign --linkers`, CPU 2, medians of 21
runs; each phase is the slowest lane's time to its last sync), for generated
programs that call libc, 200 workers per unit, written as objects and
resolved as GCC's driver links them: as a PIE (`dynamic`: the crt objects,
the program's objects, `libgcc.a`, `libgcc_s.so.1` as needed and `libc.so`'s
script, with `libc.so.6`, `libc_nonshared.a` and the loader), as a static
executable (`static`: `crt1.o`, `crtbeginT.o` and the group of `libgcc.a`,
`libgcc_eh.a` and `libc.a`, of whose members 442 load in 13 rounds), and as
the PIE with every unit but the first in an archive after it (`members`):

| Program | Kind | Objects (members) | Global symbols | Names | Vector | Exact | wild |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 1 worker | dynamic | 7 | 57 | 53 | 108 µs | 207 µs | 430 µs |
| 1 worker | static | 448 (442) | 3,980 | 1,408 | 324 µs | 675 µs | 1,530 µs |
| 2,000 workers | dynamic | 16 | 6,863 | 2,052 | 225 µs | 387 µs | 620 µs |
| 2,000 workers | static | 457 (442) | 10,786 | 3,407 | 491 µs | 908 µs | 1,610 µs |
| 2,000 workers | members | 16 (9) | 6,863 | 2,052 | 256 µs | 467 µs | 600 µs |
| 102,400 workers | dynamic | 518 | 413,919 | 102,452 | 14.1 ms | 16.7 ms | 16.1 ms |
| 102,400 workers | static | 959 (442) | 417,842 | 103,807 | 14.6 ms | 17.8 ms | 17.9 ms |
| 102,400 workers | members | 518 (511) | 413,919 | 102,452 | 18.4 ms | 24.2 ms | 14.5 ms |

wild's figure is the sum of its phases that read and resolve (`--time`:
opening the inputs, loading them into its symbol database, resolving symbols
and alternative definitions, and resolving sections), on one thread
(`--no-threads`, the same command line, medians of 11 runs). mold's (`--perf`:
`read_input_files` and `resolve_symbols`) has a millisecond's resolution, so
only the synth-size links' mean anything: 20, 23 and 18 ms for the three
kinds. For the dynamic one, mold's reading takes 9 ms where this path's
open, indices, headers and symbols take 5.3, and its resolution 11 where the
exchange, the resolution, the groups and the winners take 8.8. wild's parts
are 2.07 ms opening, 4.82 loading and 9.16 resolving.

Phase by phase, for the synth-size links by the vector path, on one lane and
on 8 (`--lanes 1,8 --cpus 2,0,1,3,4,5,6,7`: lane 0 on CPU 2, the others on
the cores that run `powersave`), in ms; the whole is `foreign_run`'s call to
its return. These measurements predate command-owned teams and include
starting and joining the lanes on every call. The current `benchmark-foreign`
starts its team once; its whole samples include dispatch and completion on
those existing threads, excluding that one-time startup and shutdown:

| Phase | dynamic, 1 lane | 8 lanes | static, 1 lane | 8 lanes | members, 1 lane | 8 lanes |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| open | 1.31 | 0.62 | 1.23 | 0.58 | 0.07 | 0.04 |
| indices | 0.08 | 0.20 | 0.13 | 0.26 | 0.90 | 1.16 |
| headers | 0.43 | 0.09 | 0.65 | 0.13 | 0.77 | 0.12 |
| symbols | 3.47 | 1.88 | 3.66 | 1.92 | 3.98 | 2.51 |
| exchange | 1.48 | 1.24 | 1.55 | 1.40 | 1.14 | 1.24 |
| resolve | 5.00 | 1.43 | 5.25 | 1.45 | 9.24 | 1.91 |
| load | 0.00 | 0.00 | 0.01 | 0.01 | 0.07 | 0.07 |
| groups | 0.24 | 0.06 | 0.33 | 0.09 | 0.18 | 0.08 |
| winners | 1.99 | 1.33 | 1.95 | 1.22 | 1.94 | 1.18 |
| whole | 14.1 | 7.59 | 14.8 | 7.57 | 18.3 | 8.92 |

- **Opening** is the kernel's: 529 inputs at 2.5 µs each (an `open`, an
  `fstat`, a private mapping and the fault of its first page), against
  about 1.2 µs for the calls alone ("The floors"). The members kind opens one
  archive for its 511 objects. Mapping every input whole with
  `MAP_POPULATE` (on the laptop) made opening ten times slower to save the
  1.6 ms that the headers' and symbols' faults cost: the mappings stay lazy.
- **The symbols** cost 8.3 ns each on the vector path against 13.8 on the
  exact one (the dynamic link, by `--linkers`' run). What is left is
  reading 10 MB of symbol table entries and 5 MB of their names from the
  files' pages, whose first touches fault, and writing 19 MB of columns and
  5 MB of names to new memory.
- **The exchange** is 3.7 ns per record against the exact path's 4.1 (3.0
  and 3.5 in a session of runs hours earlier, by the same code): the stores
  of 13 MB of records into new memory, whose first touches it pays. On 8
  lanes it takes nearly as long, 1.24 ms against 1.48: those 13 MB are seven
  huge pages, each cleared by the fault that takes it.
- **The resolution** is 12 ns per record, the probes and compares above, and
  3.5 times faster on 8 lanes. In the members kind the first object refers to
  every name, so round 0 makes 102,452 entries and looks each name up in the
  archive's index (every other unit's definitions, about 102,000 entries),
  and round 1's 310,000 records find the
  entries round 0 left cold: 9.4 ms against 5.0 for the same records in one
  round. That kind is the one wild resolves faster (14.5 ms against 18.4).
- **The winners** read every round's records again, partition by partition,
  and write each symbol's name id at its place, a store per record over
  1.6 MB, and the names' columns: 4.8 ns per record, 1.5 times faster on 8
  lanes.
- **The indices** are libc's exports and the archives' indices: 85 µs for
  the dynamic link against 248 by the exact path, and for the members
  kind's archive, whose index of about 102,000 entries is hashed eight at a
  time and copied into the index's order, 0.90 ms against 4.1.

On 8 lanes the synth-size links take half the time (7.6 to 8.9 ms), and the
small ones longer than on one lane, by an amount that varies from run to
run: over four runs, 0.23 to 0.53 ms for the dynamic link of one worker and
0.52 to 1.42 ms for its static link, against 0.11 and 0.32. Starting seven
threads and the syncs of up to 13 rounds cost more than the work ("Lanes" has
those costs), so a small link wants one lane: how many lanes a link takes is
step 8's question, with the exchange's and the winners' first touches.

#### Foreign executables, as built

`foreign_link.c` turns `foreign_run`'s result into the representation, and
`executable.c` writes it; `link-foreign` ([the command](foreign-driver.md))
puts the two behind a GNU ld command line, as GCC's and clang's drivers give
it. It writes every kind the direct path writes: static executables (with
`libc.a`, or without libc), static PIEs, dynamic PIEs and shared libraries
(#394, after the dynamic PIEs of #86).

- **The conversion.** The selected objects' sections become atoms in input
  order (a COMDAT group's loser's dropped first), each object's locals its
  symbols, each global name one symbol (its winner's, a common symbol's new
  atom, a library's import at its version, the linker's, or a weak reference
  to nothing), and each relocation a fixup, by `import.c`'s readers and their
  vector paths. The unwind records of discarded sections go
  ("Foreign COMDAT unwind pruning"); section GC, CIE pooling and string merging
  are opt-in.
- **What GNU ld and lld do that the output needs:** each needed library once
  by its soname; the definitions a library refers to or interposes exported;
  a library's own references left to the loader, which make no `--as-needed`
  library needed (lld's rule); copy relocations for a library's data that
  `-fPIE` code reads by `PC32`, with the library's other names at its address
  and lld's alignment; `__start_` and `__stop_` over the sections of a
  C-identifier name; `STB_GNU_UNIQUE` kept; the linker's names with lld's
  visibilities; and lld's regular symbol table. A dynamic PIE's own IFUNCs get
  stubs beside its imports' and `IRELATIVE` relocations, last in `.rela.dyn`,
  as lld's `.iplt` entries have them.
- **The static kinds and shared libraries (#394).** A static executable and a
  static PIE get what `dynamic.c` gives the direct path's: the start file's
  ABI note and the link's `__tls_get_addr`, which `libc.a` doesn't define and
  whose general- and local-dynamic accesses the writer relaxes, with a static
  PIE's `DT_INIT` and `DT_FINI` and no RELR (lld's `-static-pie` without
  `-z pack-relative-relocs`); the writer's static runtime handling does the
  rest (`.iplt` and `IRELATIVE`, prioritized arrays, `libc.a`'s names). A
  shared library exports every global, weak and unique definition of default
  or protected visibility, leaves a default-visibility reference to nothing to
  the loader (lld's `-z undefs`), and refuses an IFUNC that isn't preemptible
  (one the loader can't bind as a dynamic symbol), which would need an
  `IRELATIVE` relocation that the writer's shared mode doesn't write.
- **Staged chunks.** The atoms' bytes are the inputs' private mappings, so the
  writer copies each chunk into a lane's buffer and patches, hashes and writes
  the copy (`ExecutableOptions.stage`). Patched in place, each page of an input
  was copied on its first write: in CI's runs of synth's shape on one lane,
  13,508 page faults and 63.1 ms before staging, 2,253 and 53.5 ms after it
  ([40088135270](https://github.com/buster14a/buster/actions/runs/40088135270),
  [40090244483](https://github.com/buster14a/buster/actions/runs/40090244483)).

**The corpus.** `test-foreign-corpus` links each program from one command line
by `link-foreign`, lld and mold: two programs of its own (C with threads,
thread-locals, an archive, constructors, a weak reference and a
`__start_`/`__stop_` registry; C++ with iostream, exceptions across units,
RTTI, COMDAT templates and a thread) and, with `--sqlite` and `--lua`, SQLite's
shell and Lua's interpreter, each compiled by gcc and by clang at `-O2`, and
with `--self` this compiler, by clang. The three executables must run alike,
have the same `.symtab` symbols with the same sizes, and agree on `.dynamic`,
`.dynsym`'s versions and the dynamic relocations. On the 9700X every program
passes: 165 checks, 68 of them the two programs of its own that the job's list
runs ([the record](https://github.com/buster14a/buster-performance/tree/0e3f0a0708f0c01e8bfd2348beefb7fce400fc16/docs/measurements/foreign-link-2026-10-24)). What a reference linker writes otherwise as
its own valid choice is counted per program, never dropped: mold leaves weak
undefined names without a version out of `.dynsym`, keeps COMDAT signatures as
absolute locals, drops `__FRAME_END__`, binds the names a copy relocation
defines weakly, writes internal visibility as hidden (374 symbols of Lua's by
gcc) and the bounds' names local, and keeps a few GOT slots that lld and
`link-foreign` relax; lld leaves symbolic an `R_X86_64_64` to a library's data
that it scanned before the reference that copies it, and gives a function that
code both calls and loads from the GOT a second slot (9 in this compiler).

**Throughput.** `benchmark-link`'s dynamic PIEs, linked from one command line
by each linker as a process, the output on tmpfs: `link-foreign` with
`--lanes=1` pinned to CPU 2 and with `--lanes=8`, the others with one thread
(lld's `--threads=1`, mold's and wild's `--no-threads`) pinned to CPU 2 and
with their own threads on the 15 CPUs, medians on the 9700X
([the record](https://github.com/buster14a/buster-performance/tree/0e3f0a0708f0c01e8bfd2348beefb7fce400fc16/docs/measurements/foreign-link-2026-10-24)):

| Program | `link-foreign`, 1 lane / 8 | wild, 1 thread / its threads | mold, 1 / its | lld, 1 / its | GNU ld | Direct path, 1 lane |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 1 worker | **0.60** / 1.71 ms | 1.35 / 2.54 ms | 2.44 / 5.46 ms | 4.45 / 5.73 ms | 5.46 ms | 0.017 ms |
| 2,000 workers | **1.12** / 2.53 ms | 2.56 / 2.88 ms | 3.31 / 6.54 ms | 5.48 / 6.60 ms | 9.08 ms | 0.091 ms |
| 102,400 workers | **55.2** / 41.2 ms | 99.9 / 24.8 ms | 99.9 / 37.3 ms | 98.7 / 56.8 ms | 355 ms | 7.32 ms |

On the appendix's own inputs, compiled by real compilers (clang's `hello.o`,
tcc's objects and archive, and synth's 513 objects), from the appendix's
matched command line with the output on tmpfs, timed by its harness:

| Input | `link-foreign`, 1 lane / 8 | wild, 1 thread / its threads | mold, 1 / its | lld, 1 / its |
| --- | ---: | ---: | ---: | ---: |
| hello world | **0.59** / 1.61 ms | 1.34 / 2.51 ms | 2.45 / 5.30 ms | 4.46 / 5.55 ms |
| tcc | **1.06** / 2.13 ms | 2.14 / 2.93 ms | 3.29 / 6.66 ms | 5.23 / 6.51 ms |
| synth | **61.0** / 45.0 ms | 139.9 / 25.1 ms | 96.6 / 38.0 ms | 105.2 / 58.8 ms |

Every output runs and prints what lld's does.

- **Per lane, `link-foreign` writes 0.89 GB/s of output** on synth's shape
  (49.25 MB in 55.2 ms), where mold writes 0.54 GB/s on one thread (53.74 MB in
  99.9 ms), wild 0.49 and lld 0.50: 1.66 times mold's rate, in 0.55 of its
  time, with 2,257 page faults against mold's 16,093 and a peak resident set of
  235 MB against its 268. On the appendix's synth it writes 0.75 GB/s (45.9 MB
  in 61.0 ms), against mold's 0.48, lld's 0.44 and wild's 0.33.
  The estimate of 2 to 5 GB/s per lane is not reached:
  at 2 GB/s the whole link would take 25 ms, about what the resolution or the
  conversion takes alone now. `--time`'s medians of seven links on one lane:
  the resolution 21.2 ms (the symbols' parse 6.6, the resolution itself 7.2,
  the winners 2.5, the exchange 2.2, the opening 1.4), the conversion 20.3,
  the write 9.8 (its chunks 7.2) and the release 2.9, 54.4 ms in all. The
  profile's largest items are the resolution's lane loop (25% of cycles), the
  conversion's (11%) and its `link_check` (9.6%), the relocations' import
  (8.7%) and the kernel's clearing of new pages (7.8%).
- **On eight lanes** synth's link took 40.1 ms (`--time`): the resolution
  10.1 and the write 6.5, while the conversion stayed at 20.4 ms on the calling
  thread. wild with its threads takes 24.8 ms and mold 37.3. With the
  conversion on lanes too (#87, `foreign_link_lanes.c`), it takes 8.8 ms on 8
  lanes and the whole link 27.5 ms on 7, against 54.2 on one; each phase's
  limit is in [linker scaling](linker-scaling.md) ("Complete foreign links").
- **Small links are the process around them.** `--time` puts a hello world's
  link at 0.22 ms of the 0.59 ms its process takes; the rest is starting and
  ending the compiler's executable, which was a PIE until #86: its 366,684
  relative relocations made a run that does nothing take 8 to 13 ms on the
  laptop, against 1.5 ms linked at a fixed address. More lanes cost a small
  link their threads' start: 1.71 ms on eight for one worker.

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
// Starts a command's fixed team, with the calling thread as lane 0; team starts zero-initialized.
static bool lane_team_start(LaneTeam* team, uint32_t capacity, const uint32_t* cpus);
// Joins the team's workers. Also safe after a failed start or with a zero-initialized team.
static void lane_team_stop(LaneTeam* team);
// Runs entry(argument) on count lanes of the current team, the caller being lane 0, and waits for all to finish.
// Without a command-owned team, starts and joins a temporary group, as the cold-start tests and benchmark require.
static bool lane_run(uint32_t count, void (*entry)(void* argument), void* argument, const uint32_t* cpus);
```

**The command owns the threads.** After validating its options, a parallel
command starts one `LaneTeam` with its chosen capacity and CPU assignment,
uses it for its whole operations, then stops it once during cleanup. Every
phase within an operation remains a direct function call made by all its
lanes. The team is fixed: a run can use an active prefix, such as 1, 8, 2 and
8 lanes, and the same workers return at the same indices. The other workers
sleep on futexes. A one-lane team starts no worker threads.

Startup must create and pin every requested worker before the team is
installed; a failure joins every worker it did create. An operation with too
many lanes, a different CPU assignment or a nested `lane_run` fails without
calling its entry or starting replacement threads. `lane_alone` does not
permit nesting. The entry's shared writes are visible when `lane_run` returns;
each worker releases its scratch arenas after the operation, while the
calling thread keeps its scratch and gets its prior lane context, counter and
CPU affinity back. Dispatch/completion synchronization is separate from the
barriers inside an operation.

`benchmark-files`, `test-files`, `benchmark-foreign`, `benchmark-link` and the
output modes of `benchmark-executable` own teams. Each handler starts and
stops its team in the translation unit that runs the operations: the split
build's copies of `lane.c` have separate thread-local contexts. The ordinary
preprocess/parse command still runs on one lane; the broader frontend work is
tracked in [#149](https://github.com/buster14a/buster/issues/149).

The lane lives in a thread-local context with the thread's scratch arenas, as
`TCTX` holds the RAD Debugger's `LaneCtx`. A thread that has to run a phase
alone, while the others do something else, makes itself a group of one (index
0, count 1, no barrier) with `lane_alone` and restores its group afterwards.
The barrier is a counter and a generation word: it spins briefly, waits in
`mwaitx`, then sleeps on a futex. Within an operation, its atomics and the
counter `lane_take` adds to provide the lane coordination: one atomic add per
part, and none with one lane. The command's team also synchronizes dispatch
and completion between whole operations.

Three things differ from the RAD Debugger, each measured below:

- **The calling thread is lane 0.** radbin's main thread waits for its lanes;
  here it is one of them, so a group of one starts no thread. A command starts
  its team once and reuses it for its operations, with each operation's phases
  in its entry point, since starting threads costs as much as 180 to 220 syncs.
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
- **Starting:** `lane_run` outside a command-owned team, of an entry point
  that returns at once, without pinning, from the call until it returns. This
  remains a cold create/join measurement. That is what 180 to 220 syncs cost,
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
| Patching fixups | loads and stores | scalar, 32-bit targets (done: `executable_patch_pc32` and `executable_patch_mixed`, measured above and, on a mix of kinds, in "Patching and writing": 0.97 ns against 1.72 for gathers and scatters) |
| Writing ELF records (`Rela`, `Sym`, dynamic tables) | stores | scalar (measured: ties the `vpermt2q` transpose) |
| Reading ELF records (objects, archives, `.dynsym`) | shuffles, compression and scalar joins | AVX-512 with an exact path (done: 2.1 against 22 ns per record; "Step 5's phases, data-parallel") |
| Copying chunks | bandwidth | `memcpy`; the chunks stay in L2 |
| Hashing names | arithmetic | vectorized, one name per masked load, several in flight |
| Reading foreign objects' global symbols (the foreign path's parse) | shuffles and masked loads | AVX-512 with an exact path, eight symbols at a time with their names hashed and copied eight at a time (done: 8.3 against 13.8 ns per symbol for the synth-size link; "Steps 1 to 5, as built") |
| The foreign path's exchange | stores to every partition | the records built eight at a time by a transpose and stored from the registers, with an exact path; the counts a histogram of the hashes' top bits on both (done: 3.7 against 4.1 ns per record, bound by the records' new memory; a batch stored to memory and copied a record at a time lost to the exact path) |
| Resolving foreign names (a partition's table) | each compare's wait for two names' bytes | scalar probes per record on both paths, each record's name prefetched ahead, and its slot, entry and entry's name where an earlier round made the entry (done: 12 ns per record): grouping a partition's records by name first with a radix sort, to probe each name once, lost (7.7 against 5.3 ms for the synth-size link before the prefetches; "Steps 1 to 5, as built") |
| Splitting strings at NULs | arithmetic | the lexer's bitmask-to-positions method |
| Decoding and classifying relocations | shuffles and tables | `vpermt2q` transpose, `vpermb` classification, `vpcompressd` lists |
| Bitsets (liveness, GOT needs) | bandwidth | 512-bit OR, `vpopcntq` for counts |
| Prefix sums for layout | latency | atoms 16 at a time, a run of one bucket's padded sizes summed or scanned in vectors, the rest scalar (done: "Layout"); the counting sorts' are vectors (`import_exclusive_scan`) |
| Build ID | arithmetic | AES rounds on each chunk while it is in L2 (done: 260 GB/s, as fast as reading it; "Patching and writing") |
| Probing hash tables | memory latency | scalar loads with software prefetch: gathers measured no faster than scalar loads in the patch kernel |
| Matching a program's names against the runtime's (binding) | the index's cache lines, for lookups that mostly miss | a blocked Bloom filter in L1 tested eight names at a time by two gathers, the index only behind it (done: 0.85 against 6.8 ns per global symbol; "Step 5's phases, data-parallel") |
| A static link's closure over the archives' members (binding) | each member's references | rounds over a bitset of the members, references 16 at a time compressed onto a list that is marked after the round: marked per batch, each waited for its masked store (done: 1.43 against 2.44 µs, 3.24 against 5.51 ns per member; "Static links, as built") |
| A static executable's IFUNCs and stubs (the writer's count) | the fixups' targets, against tens of IFUNCs | a filter by two hashes in four registers, 16 targets at a time: one hash took a 46th of them for IFUNCs (done: count 1,309 against 1,588 µs for 2.35 million fixups; "The static executable with libc") |
| Relaxing general- and local-dynamic TLS accesses | a compare and a copy per access | scalar, in the patch's path for the other kinds: the most a test program has is 47 accesses, whose five atoms' patching takes 0.89 µs of a 125 µs write |
| Every executable's loads and calls through the GOT that relax (the count, and the patch) | which of a link's reads of slots may relax, and which targets need a slot | compares of 64 kinds and addends find the candidates, marked in a bitset of their own that the reduction ANDs with the relaxable symbols 64 at a time (those a symbols' pass finds 64 at a time from the source and type columns, or a PIE's RELATIVE bitset); each is rewritten in the patch's path for the other kinds, a byte or two (done: the count 2.01 against 2.40 ms with gathers of the bitset's words for 180,000 workers; "The static PIE" and "GOT relaxation in every executable") |
| Ordering prioritized init and fini arrays | a few entries | scalar on lane 0, a stable radix sort: six entries take 0.24 µs of a 99.5 µs write |

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
  GNU as keeps a named local symbol for a `.reloc` where it names the
  section's symbol for an instruction, so the comparison matches a
  relocation's target by where it points, and by its form only where that
  matters: the GOT, TLS and size types, and into `SHF_MERGE` sections.
  `test-object-writer` also reads every object back itself and works out from
  the representation alone what each section, symbol and relocation must be,
  past `SHN_LORESERVE` sections too, and checks that lanes write the same
  bytes. 49 copies of the writer, each with one rule broken (a type, an index,
  a flag, a gap's byte, the section symbol rule, the file's handling), all
  fail it.
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
  lane short of scratch memory at every amount, from none up. `test-static`
  writes programs bound against the static runtime, generated and compiled
  ones, which must run as `cc -static`'s links of the same objects do, and
  checks their IFUNCs' stubs and IRELATIVE relocations, every relaxed TLS
  access, every relaxed load and call against the instruction it was, the
  GOT's slots, the prioritized arrays' order and the linker's names; with
  `--cross-check`, they must run as lld's links do too, with as many
  IRELATIVE relocations, the same TLS alignment and the same arrays' sizes,
  and as many GOT slots as lld's, but for thread-locals' offsets and the
  slots that lld's non-PIC immediates replace.
  It writes them as static PIEs too, which must run as `cc -static-pie`'s
  links do (or be refused as those are), and checks their `.dynamic`, every
  RELATIVE relocation in `.rela.dyn` and `.relr.dyn` (with `--cross-check`,
  as many as lld's, and as many GOT slots but for thread-locals' offsets),
  and a write over the relaxed bytes. `test-executable` and `test-dynamic`
  check every relaxed load and call by the same rule, and their GOTs.
- **The runtime snapshot:** `test-snapshot` loads each kind's runtime back
  from its snapshot and compares it with the import, binds and writes the same
  programs against both into the same bytes, and checks staleness, damage and
  concurrency. A damaged directory must make the file invalid. A byte changed
  in a piece's records must leave every piece current but that one, fail a
  binding that reads it, and let one that doesn't bind as before.
- **Lane counts:** each phase with 1, 2, 3 and 8 lanes, outputs compared byte
  for byte. `test-executable` writes every program so, each time from zeroed
  fields over the last file scribbled over, so that a lane that skipped a
  field or a chunk shows.
- **Foreign links,** against lld and mold on the inputs above and a larger
  corpus: same exit status and output when run, the same set of symbols with
  the same sizes, and `readelf` checks of the dynamic section, versions and
  relocations. `test-foreign-corpus` does it, for programs compiled by gcc
  and clang ("Foreign executables, as built").

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

**Not built (#89).** #88's measurements leave it little to save:

- what a rebuild could save by patching pages in place is what a full link
  costs after the compiling: the direct path writes synth's shape, 44 MB, in
  7.3 ms on one lane ("Foreign executables, as built"), and streaming leaves
  5.0 ms after the last of 102,400 functions, the tail of an 86 MB file
  ("Streaming, as built");
- every link would pay for the bookkeeping that saving needs: the reverse index
  of every fixup's target, kept from build to build, and the stable layout's
  slack, which the canonical layout doesn't have; and a change outside the fast
  path (a binding, a COMDAT winner, GC's roots, TLS, the dynamic symbols, the
  options) falls back to the full link anyway.

At a few milliseconds a rebuild, against those costs on every link, the mold
paper's argument holds here. What would change it is an output whose full write
takes far longer than an edit's recompile, hundreds of megabytes; until a
workload like that is measured, the full link stays the only one.

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
   per-object costs against the file floor. Done: `object_writer.c`,
   `test-object-writer` and `benchmark-object-writer`, whose costs are in "The
   object writer (`-c`)".
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

   Done, in four parts: the runtime's files found and imported, each reader
   with a vector path and an exact one (`import.c`, `runtime.c`,
   `test-import` and `benchmark-import`: "The runtime snapshot" and "Step 5's
   phases, data-parallel"); the runtime in the program's link, bound
   (`bind.c`, `test-bind` and `benchmark-bind`: "Step 5's phases,
   data-parallel"); the dynamic PIE (`executable.c`'s dynamic mode,
   `dynamic.c`, `test-dynamic` and `benchmark-executable`'s PIE rows: "The
   dynamic PIE"); the snapshot itself (`snapshot.c`, `test-snapshot` and
   `benchmark-snapshot`: "The runtime snapshot"), `.eh_frame` for the
   generated programs and `.eh_frame_hdr` ("The unwind tables"), and
   `benchmark-link`, which meets the targets ("The whole link"), with
   `benchmark-floors`, the appendix's floors.
6. **`-static`:** libc.a's closure, IRELATIVE for its IFUNCs, static TLS.
   Then `-static-pie` and `-shared`.

   The first part is done: the static runtime's files found and imported
   (`runtime_import_static`, with COMDAT groups, IFUNCs, absolute symbols, the
   general- and local-dynamic TLS kinds and prioritized arrays in `import.c`
   and `link.c`, and each member's references and groups for the closure:
   "A static executable's runtime, as built").
   The second part is done: binding against it (`bind.c`: the closure over
   the members in rounds, their joins in archive order with COMDAT groups
   discarded, and every name bound without a shared library: "Static links,
   as built").
   The third part is done: the static executable with libc (`executable.c`'s
   mode with `ExecutableOptions.runtime`, `dynamic_static_inputs`,
   `test-static` and `benchmark-executable`'s static rows: IFUNCs through
   `.iplt` and IRELATIVE relocations, the general- and local-dynamic TLS
   accesses relaxed, and the prioritized arrays: "The static executable with
   libc"). The `__start_` and `__stop_` symbols wait for foreign inputs (step
   7): neither system's static runtime refers to one.
   The fourth part is done: the static runtime's snapshot (`runtime_open_static`,
   format 4's directory and pieces checked as binding reads them,
   `test-snapshot` and `benchmark-snapshot`'s static rows: "The runtime
   snapshot") and `benchmark-link`'s `-static` rows ("The whole link").
   The fifth part's first half is done: the static PIE (the static PIE's
   runtime, `runtime_import_static_pie`; `executable.c`'s static PIE with GOT
   relaxation and RELR, `dynamic_static_pie_inputs`, and `test-static` and
   `benchmark-executable`'s static PIEs: "The static PIE"). So is GOT
   relaxation in every kind, with lld's sharing of an IFUNC's stub slot ("GOT
   relaxation in every executable"). Its second half, `-shared`, has its
   runtime and binding: the loader as a library of the dynamic runtimes,
   needed as `libc.so`'s script says ("The runtime snapshot", "The dynamic
   PIE"), and a shared library's runtime (`runtime_import_shared`,
   `runtime_open_shared`) and binding (`bind_shared_exports`: "Shared
   libraries, as built so far"). The writer is now implemented for preemption,
   exported definitions and initial-, general- and local-dynamic TLS, with generated and compiled
   libraries checked against lld and run through `dlopen` ("The shared
   library"). Local-exec accesses in shared libraries remain invalid by the ABI.
7. **Foreign inputs:** objects, archives, shared libraries, the vector parse
   and the exchange. Done when the differential tests against lld and mold
   pass on a corpus.

   The first part is done: steps 1 to 5 of "Linking other compilers'
   objects" (`foreign.c`, `test-foreign` and `benchmark-foreign`: "Steps 1 to
   5, as built"): the command line opened, the archives' indices read alone,
   each round's objects parsed, their records exchanged and resolved, the
   members the link needs loaded in rounds, and COMDAT groups picked, as GNU
   ld picks them where its result doesn't depend on the archives' order and
   as lld does where it does, which the tests check against both.
   The second part is done: the loaded objects into the representation and
   written by `executable.c` as static executables without libc and dynamic
   PIEs, by `link-foreign`, with section GC, CIE pooling and string merging
   opt-in; the differential tests against lld and mold pass on the corpus
   (`test-foreign-corpus`: two programs of its own, SQLite's shell, Lua and
   this compiler, each by gcc and clang), and the per-lane throughput is
   measured against mold's and wild's single thread ("Foreign executables, as
   built"). Static executables with libc, static PIEs, shared libraries and the
   dynamic PIE's own IFUNCs remain.
8. **More than one lane:** measured scaling, and the per-filesystem write
   choice. Done ([linker scaling](linker-scaling.md)): the direct path's and
   the foreign link's phases on 1 to 8 lanes, one per core, and on the SMT
   siblings to 15, with each phase's limit, those still fastest on 15 bounded
   by the machine's CPUs, past which lanes only lose; the foreign conversion on
   lanes (`foreign_link_lanes.c`), and `link-foreign` capped at a lane per CPU;
   and the write method, `pwrite` on tmpfs and btrfs, since mapping the output
   lost on btrfs and was mixed on tmpfs. The lane tests' outputs stay byte for
   byte alike on 1, 2, 3 and 8 lanes.
9. **Unwind tables from the code generator's prologues, and DWARF,** then
   **streaming the output** in the data-first order, measured against writing
   at the end. Done: the unwind tables from Tier 0's prologues (`unwind_tier0.c`
   and `unwind.c`, "The unwind tables") and DWARF at final addresses
   (`debug.c`), which gdb and perf read (`test-unwind`, `test-debug`); and
   `streaming.c`, which writes a program while Tier 0 compiles it, in the
   data-first order, with both, the DWARF in units made as the code compiles
   ("Streaming, as built"): gdb, perf and llvm-dwarfdump read it, and 5.0 ms
   follow the last of 102,400 functions against 18.9 written at the end.
10. **Incremental output,** if the measurements in 9 leave it worth doing. Not
    built: they don't ("Incremental output").

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
  commands and the methods. The harness is now `benchmark-link` and the floor
  programs `benchmark-floors`, inside `build/compiler` like every other
  benchmark.

## Appendix: how the numbers were measured

**The machine.** A Ryzen 7 9700X (Zen 5) running Arch Linux with kernel
6.18.50-2-lts:

- 15 CPUs online (CPU 10, CPU 2's SMT sibling, is offline);
- amd-pstate-epp in active mode with the performance governor for the link
  measurements. For the lanes' and the object writer's (2026-10-19), only CPU 2
  had it, and the others ran the powersave governor with the energy preference
  `balance_performance`;
- tmpfs at `/tmp` and btrfs (zstd:3) at `/home`;
- glibc 2.44, GCC 16.2.1's crt files and libgcc, clang 23.1.1;
- GNU ld and readelf 2.47, lld and llvm-readobj 23.1.1, mold 3.0.0 and wild
  0.10.0.

Every run took the machine's lock (`~/buster-fast-bench/exclusive.sh`), one
run per lock. One-thread runs are pinned with `taskset -c 2`. The scripts are
in `~/buster-fast-bench/linker-baseline/` there, with the inputs below. The
harness is now `benchmark-link`, which takes the linkers' times this way on
generated programs ("The whole link"), and the floor programs are
`benchmark-floors`.

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
`/dev/null`, and waits for it (`benchmark-link` spawns it with `posix_spawn`).

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

**File and memory floors**, which `benchmark-floors` takes. One thread on CPU 2,
medians of 15 runs (5 at 64 MB and above).

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
  return. `benchmark-floors`'s child sends the time it sends the message, from
  which the parent measures: on the parent's CPU, the child can exit before
  the parent reads the message. It runs the child on another CPU than the
  parent's when the parent may use one.
- **Small calls:** 1,000 of each, timed together.

**The object writer.** `benchmark-object-writer` pinned to CPU 2, with the
options of each program in "The object writer (`-c`)" (`--function-sections`;
`--units 1 --functions 12800`; `--units 64 --functions 4 --filler 4`), and
`--directory` in `/home` for btrfs.

- **Phases:** a harness reading the hardware counters (user mode) around each
  phase of `object_build`, over 20 passes of the 64 synth-like objects, less
  the reads' own cost.
- **Ways to write the file:** a program like the benchmark's files pass for a
  155 KB buffer. Each way writes 64 files of its own, with 15 µs of spinning
  before each write (about a build), and the ways take turns going first.
  Medians of 21 passes.

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

## Boundary audit #298

Audit pin: `42630114aeb0553c3e2fabfd7fe9bc84b0ce6126`;
[scope and evidence](simd-boundary-audit.md). Pinned sources:
[Link](https://github.com/buster14a/buster/blob/42630114aeb0553c3e2fabfd7fe9bc84b0ce6126/src/buster/compiler/link.c#L4),
[object writer](https://github.com/buster14a/buster/blob/42630114aeb0553c3e2fabfd7fe9bc84b0ce6126/src/buster/compiler/object_writer.c#L4),
[import](https://github.com/buster14a/buster/blob/42630114aeb0553c3e2fabfd7fe9bc84b0ce6126/src/buster/compiler/import.c#L4),
[foreign resolution](https://github.com/buster14a/buster/blob/42630114aeb0553c3e2fabfd7fe9bc84b0ce6126/src/buster/compiler/foreign.c#L4),
[foreign handoff](https://github.com/buster14a/buster/blob/42630114aeb0553c3e2fabfd7fe9bc84b0ce6126/src/buster/compiler/foreign_link.c#L4),
[snapshot](https://github.com/buster14a/buster/blob/42630114aeb0553c3e2fabfd7fe9bc84b0ce6126/src/buster/compiler/snapshot.c#L4),
[dynamic inputs](https://github.com/buster14a/buster/blob/42630114aeb0553c3e2fabfd7fe9bc84b0ce6126/src/buster/compiler/dynamic.c#L4),
[executable](https://github.com/buster14a/buster/blob/42630114aeb0553c3e2fabfd7fe9bc84b0ce6126/src/buster/compiler/executable.c#L4).

| Producer → consumer | Implemented handoff and other readers |
| --- | --- |
| Tier 0/assembler/test records → encoding/fixup ends → `link_add_code_fixups` | Code atom bytes plus symbolic references become offset-sorted, nonoverlapping fixups. Marked-instruction order, field kind and source addend survive. Object and direct writers both consume them. |
| Checked scalar definitions → `check_data_input`/`check_data_emit` → Link | Canonical object-to-symbol IDs, atom bytes and address fixups; current supported scalar subset only. Tests write objects and execute linked fixtures; the checker CLI dumps this output. |
| Frontend/import/generator names → Link names → binding/object/string tables | Counted spelling, NUL for ELF, length and retained u64 `link_hash`. Names are not automatically interned; the producer supplies one ID per spelling where required. |
| ELF bytes → `import_object`/archive/library/script readers → Runtime or Foreign | Checked section/symbol/relocation columns and input-index mappings. Eight-record vector paths decline to exact readers; archive indices support lazy member loading. |
| File search/import → `runtime_import*` → snapshot → `runtime_open*` → binding | Counted runtime pieces, global-name index/filter, archive refs/COMDAT and library versions. Mapped snapshot pointers/deltas avoid rebuilding columns; directory validation precedes use, piece hashes are checked when consumed. |
| `foreign_run` parse → exchange/resolve → archive frontier → winners/COMDAT | Stable input/member positions, hashed name partitions and selected definitions. Count/place/fill phases have collective proceed decisions; a round ends only when no new member/library reference loads. |
| Foreign winners → `foreign_link_build*` → canonical Link/dynamic inputs | Reuses resolved names and selected members; input-local symbol maps and original input order preserve references and initialization order. No second name resolution. |
| Program Link + runtime pieces → binding → `dynamic_*_inputs` → `executable_write` | Canonical symbol bindings, imports/exports, library/version arrays, TLS/runtime metadata. Bind before selection is the intended source integration; current generators/foreign inputs exercise consumers. |
| Link → `object_prepare`/`object_map_symbols` → `object_build`/`object_write_units` | Group units/classes, count/place exact ELF ranges, fill once. One lane owns an entire object, with its own symbol-map/scratch. Other linkers and `import_object` are output readers. |
| Link + options → executable symbol classes/count/place → resolve | Class/alignment buckets and prefix offsets, dynamic symbol/GOT/relocation metadata, atom addresses and symbol references. Ordered init/fini/unwind classes preserve required order. |
| Final layout/references → chunk patch/hash/write → loader/debug readers | Disjoint chunk work and collective barriers; patched bytes, build ID and published file. Native debug/source-event producers and actual compilation overlap remain [#88](https://github.com/buster14a/buster/issues/88). |

### Memory, ownership and ordering

Link uses u32 IDs (zero absent) and counted SoA tables. Atom sizes/addresses and
name hashes are wide; fixups use u32 offset/target/addend and a byte kind
(13 logical bytes per fixup). Wide addends are indexed in a side table and
accessed through `link_fixup_addend`; a consumer must not interpret the index
as the addend. Every atom's fixups form one contiguous sorted range; fields
cannot overlap or extend outside its bytes. A capacity failure is sticky and
an add returns zero. `link_check` validates whole-representation invariants
including local-unit and TLS/reference-kind constraints.

Tables/name arenas and atom bytes have different owners. Object writing reads
atom bytes. Executable patching **writes them in place**, as well as its owned
address/reference/GOT/dynamic-index columns. Source-generated bytes and private
foreign mappings must remain writable and alive until writing finishes.
`foreign_link_build` borrows `Foreign` mappings; its failure may leave partial
caller-owned output. Restore/rebuild pristine inputs before comparing a second
writer variant. It is not the static-data emitter's transactional contract.
Do not run object writing concurrently with executable mutation of the same
bytes or reuse scratch as retained atoms.

Import reads untrusted counted bytes with checked offset arithmetic and
`memcpy` field loads; no input alignment/padding is assumed. Snapshot regions
are 64-byte aligned and layout/version/executable-identity checked; that format
is not a blanket permission for a new consumer to vector-read outside a region.
Record kernels use bounded masks/tails or their explicit internal allocations;
encoder output slack must not be counted as atom bytes. A serialized object is
one exactly sized buffer. Zero-fill atoms have no byte payload, and empty units
still have object-output semantics.

`ExecutableDynamic` borrows counted library/version arrays. Library indices are
u32, version indices are unique 2..0x7fff and grouped by library; required
unversioned libraries are not inferred only from version use. Preserve selected
input order. Binding/resolve completion precedes final layout and relocation
patching. Archive rounds, COMDAT choice, init/fini priorities and version
numbering are semantic boundaries; arbitrary reordering for batch fullness is
invalid. Lanes reserve once, fill disjoint ranges, synchronize and make the same
failure decision. Independent complete objects/functions can be distributed;
one ELF record's 8/16/64-lane kernel width is not the whole-link batch size.

### Waste, alternatives and disposition

The encoded-to-Link boundary already retains field kinds, addends and targets;
re-disassembly would lose useful producer knowledge. Reusing canonical foreign
winners already removes repeated global resolution. Snapshot loading already
avoids reparsing unchanged runtime objects. Exact ET_REL construction already
avoids per-symbol allocations and buffer growth. These are retained designs,
not new optimizations in the audit.

Remaining observable passes include unit/class grouping for ET_REL, executable
class/relocation counting and patch/hash/write, input-index remaps, and cold
snapshot/file checks. Each serves a different consumer or publication boundary;
removing it requires proving its data unused or available earlier. Compare
producer-supplied counts/order with current count/place/fill, or bounded streamed
publication with materialization, charging metadata maintenance, synchronization,
bytes, file layout and every reader. Existing measurements rejected an isolated
ELF relocation transpose; stores and the full object path matter.

Disposition: retain the current shared Link and writer protocols. `test-link`,
object/import/bind/snapshot tests, native dynamic/static/foreign tests and
lane/determinism tests exercise scalar/exact/vector, poisoned output, invalid
metadata, capacities, guarded inputs, schedule changes and external ELF/runtime
oracles. Existing `benchmark-link` includes runtime opening, binding and writing
for generated programs; it excludes real C production. Object/foreign kernel
results and separate phase rows are not full compiler measurements.

The active linker owner retains [#80](https://github.com/buster14a/buster/issues/80)/[#85](https://github.com/buster14a/buster/issues/85)/[#86](https://github.com/buster14a/buster/issues/86)/[#87](https://github.com/buster14a/buster/issues/87)/[#88](https://github.com/buster14a/buster/issues/88) and [#145](https://github.com/buster14a/buster/issues/145)'s output boundary.
Pending [#316](https://github.com/buster14a/buster/pull/316)/[#328](https://github.com/buster14a/buster/pull/328)/[#329](https://github.com/buster14a/buster/pull/329)/[#332](https://github.com/buster14a/buster/pull/332) and the owner's GC/symbol/unwind/streaming work are
not silently included at this pin. Missing source-generated symbols/CFI, general
foreign qualification, shared dynamic TLS and true compilation/output overlap
remain linked acceptance work. Completion requires matched small/large actual
inputs and complete producer + conversion + writer costs, bytes/s and ns/cycles,
live/scratch memory, allocations, bytes moved, fallback and lane occupancy,
plus identical output/execution and debugger/profiler consumers where relevant.
