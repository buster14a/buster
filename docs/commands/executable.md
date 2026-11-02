# executable

Run commands and resolve source paths from the repository root. Follow the
[shared agent rules](../../AGENTS.md), including the 9700X lock and `cores`
requirements. [Command index](README.md).

## Module overview

- `ExecutableOptions.symbols` provides optional regular symbol output without
  DWARF. `test-executable` covers the attachment and unchanged-destination
  refusal; `test-debug --cross-check` covers the debug-only consumers. The
  [symbol-output contract](../../docs/executable-symbols.md) records producer
  ownership, supported definitions and pending parallel/performance gates.

- `src/buster/compiler/executable.c` writes the direct path's executables: a
  link in that representation laid out, patched and written with a build ID
  by every lane of a group, as a static executable without libc (step 4 of
  the design's plan), as a dynamic PIE against glibc (step 5's third part),
  with its dynamic symbols, stubs and relocations, RELR among them, as a
  static executable with the C runtime (step 6's third part), with its
  IFUNCs' stubs and IRELATIVE relocations, its general- and local-dynamic
  TLS accesses relaxed to local-exec and its init and fini arrays' priorities,
  as a static PIE (step 6's fifth part): the PIE's layout and RELATIVE
  relocations without dynamic symbols, and the static runtime's IFUNCs with
  their IRELATIVE relocations last in `.rela.dyn`. Every kind's loads and
  calls through the GOT of the image's symbols are relaxed, as GNU ld and lld
  relax them, and an IFUNC whose stub only calls reach reads the stub's slot
  through the GOT, as lld's does; a dynamic PIE takes its own IFUNCs too, their
  stubs before its imports' and their `IRELATIVE` relocations last in
  `.rela.dyn`, and relaxes its general- and local-dynamic TLS accesses
  (`ExecutableDynamic.tls_get_addr`, #515): to local-exec, or to initial-exec
  through a `TPOFF64` slot for an imported thread-local; all from vector
  passes with exact twins.
  It also writes shared libraries (`ExecutableDynamic.shared`, with optional
  `soname`): default-visibility global and weak definitions are preemptible,
  calls use non-lazy GOT stubs, and initial-exec TLS uses TPOFF64 slots.
  General-dynamic TLS uses a separate two-word GOT pair per symbol and
  local-dynamic TLS one module pair, preserving `__tls_get_addr` calls;
  DTPMOD64/DTPOFF64 relocations and module offsets are emitted as appropriate.
  Regular shared-library outputs use a new file and rename, preserving any
  old inode that a process still maps.
  Local-exec accesses and undefined DTPOFF32 module offsets in shared
  libraries are refused. Distinct general-/initial-exec slot columns allow
  both models to reference the same symbol. TLSGD target bitsets are reduced
  by whole symbol words, then expanded into slots with an exact scalar twin.
  `dynamic.c` gives it what a PIE needs from the C runtime and from
  binding (`dynamic_inputs`), what a static executable with the C runtime
  needs (`dynamic_static_inputs`) and what a static PIE needs
  (`dynamic_static_pie_inputs`), and what a shared library needs without a
  start object or interpreter (`dynamic_shared_inputs`). `executable_test.c`
  holds the static executables' tests without libc (`./build/compiler test-executable` runs
  them), `dynamic_test.c` the PIEs' (`./build/compiler test-dynamic`) and
  `static_test.c` those with libc and the static PIEs' (`./build/compiler
  test-static`); `main.c` includes all three, and `executable_benchmark.c`
  provides `benchmark-executable`.
  `ExecutableDynamic` borrows counted library/version arrays, so foreign
  producers can supply selected dependencies in input order. A version's
  library is a 32-bit array index; version indices are unique 2..0x7fff and
  grouped by library. Required libraries include unversioned dependencies;
  optional ones are selected by used versions. `dynamic_libraries_test.c`,
  included by `dynamic_test.c`, checks high library indices, foreign imports,
  malformed metadata and exact/vector identity at 1/2/3/8 lanes. Missing import
  version definitions are refused in the existing symbol pass before output.
## Direct executable tests

`./build/compiler test-executable` (about 0.2 s on the 9700X) checks
`executable.c` in a directory of its own under `--directory` (`TMPDIR` or
`/tmp` by default), which it removes unless `--keep` says to leave it:

- the build ID's chunk hash: every length up to 2,100 bytes, cut at random
  places, hashes as it does whole, and zeros of each length up to a page hash
  apart from the next length's;
- test-link's program sets, and one of 50,000 workers in 38 chunks, each
  written by groups of 1, 2, 3 and 8 lanes, every time from zeroed fields over
  the last file scribbled over, so that a lane that skipped a field or a chunk
  shows. The files and build IDs must be byte-identical, and each program must
  run and print what its model predicts. In each file every fixup's field must
  hold its kind's value; a load or call through the GOT that relaxes (a
  `CALL_GOT` or `LOAD_GOT` with the addend -4 to a symbol of the image's that
  isn't an IFUNC: `executable_test_relaxes`, the test's own rule) must have its
  instruction rewritten (`mov` to `lea`, `call` to `addr32 call`, `jmp` to
  `jmp` and a `nop`) and every other must keep its own; the GOT must have a
  slot for each target of a GOT kind that a fixup reads unrelaxed (numbered
  from 1 in symbol order, holding its value), the headers must
  have lld's layout, no chunk boundary may split a field, and the build ID
  must be the tree of the file's chunks. The unwind tables, read back as an
  unwinder reads them (`executable_test_unwind`): `PT_GNU_EH_FRAME` exactly
  over `.eh_frame_hdr`; its version, encodings, pointer to `.eh_frame` and
  count of the FDEs that a walk of `.eh_frame` finds (a terminator only last);
  its table sorted, each entry an FDE and its initial location, each FDE
  once; a lookup of each FDE's first and last byte, by libgcc's binary
  search, finding it, and one of each function finding its FDE, of the
  function's size; and the write's count of FDEs, which must have come in
  order. The exact twins must write the same file with 1 and 8 lanes. A copy
  of the defaults' program with one pointer 8 bytes off must print something
  else, and the two sets that call libc (step 5's) must be refused, an import
  named, without creating the output;
- chunk boundaries: programs with a call's rel32 0 to 4 bytes before the
  nominal 256 KB boundary and an `ABS64` 0 to 8 bytes before 512 KB, both in
  atoms larger than a chunk, and with a call through the GOT (`call [rip +
  slot]`, which relaxes to `addr32 call` and so rewrites the 2 bytes before
  its field) whose 6 bytes start 0 to 6 bytes before 256 KB. A boundary must
  move to the end of a field (or such an instruction) that crosses it and stay
  where it is otherwise, with every lane count, and the program must run;
- the output file: created; overwritten in place by a smaller program (and
  truncated) and with its mode restored; written beside it and renamed over it
  when it has another link (which keeps its bytes), when it is a symbolic link,
  when a process runs it (which runs on), when it is someone else's (checked
  only when the test may `chown`, as root), past a file a killed link left
  under the name without random digits, and with a name of `NAME_MAX` bytes;
  and written in order into a FIFO, which a failed link leaves in place: for
  a reader that opens it and reads to the first end of file, with inotify
  counting one close after writing, and for one that holds the read end of a
  FIFO that may be written but not read (which only binds a user other than
  root). A directory and a path under a regular file are refused, naming the
  call. A link with an undefined symbol fails, naming it, and leaves the
  output alone; one whose `ABS32` falls below 0 removes it, and so does one
  whose `PC32` leaves int32's range, which only the checked path sees; one
  that fails while writing beside the output leaves the output, its other
  link, and nothing beside them;
- limits: a weak thread-local symbol defined nowhere (offset 0 in its field
  and its GOT slot); TLS blocks of 2 GB (linked) and past it, atoms past 2 GB
  and one whose padding wraps (refused); a bad entry point, the first of two
  symbols defined nowhere in different lanes' ranges, the smallest failing
  fixup, an atom aligned past a page, a bad init array atom and an atom of
  each of the C runtime's classes (`.init` and `.fini`), which step 4's
  executables have no section for, each reported alike with every lane count;
  and a lane with no scratch memory to spare, then 64 bytes more at a time,
  until the link succeeds: every failure must be `EXECUTABLE_ERROR_MEMORY` and
  leave no output;
- the unwind scanner: `.eh_frame` atoms of each CIE `.eh_frame_hdr` indexes
  (GCC's in versions 1 and 3, with a personality, an LSDA and a signal frame,
  and with a personality's pointer absolute or a LEB128) and of seven it
  doesn't, with up to 40 FDEs and with or without a terminator, each as built
  and changed in each of 19 ways: records too short, cut after `pc_begin`, too long,
  pointing into their CIE or past themselves, without `pc_begin`'s fixup or
  with an `ABS32` there, trailing bytes and one CIE past the walk's eight
  (refused); a terminator at the end or before an FDE, a second CIE, eight
  CIEs, a fixup among the instructions and a wide addend; and initial
  locations at int32's limits from the header and one past each. The walk
  must count the FDEs the test knows of, or refuse the atom; the vector path
  must count the same or leave the atom to the walk, and take every atom of
  one CIE and its FDEs; and with table entries, both must give a reference's,
  or refuse an entry outside int32;
- the sort: tables sorted with equal neighbors, sorted but for one pair,
  reversed, random, all equal, and from `INT32_MIN` to `INT32_MAX`, of every
  count to 200 and some to 5,000: the check must say whether they are sorted,
  and the sort (always, for the exact twin) give a stable sort's order;
- unwind links: 40 functions aligned so that the layout's buckets put them
  out of their FDEs' order, in 12 `.eh_frame` atoms of the CIEs the header
  indexes, written by every lane count and by the exact twins into the same
  file, whose table must come out sorted and whose program must run; an
  initial location at `INT32_MAX` from the header (linked) and one past it
  (refused, naming its atom), and one from a wide addend (linked); and the
  first of two atoms the header can't index, in different lanes' ranges,
  refused alike by every lane count, and one before a symbol defined nowhere,
  refused for the atom: by the count, before the layout;
- blocks: 24 random links from the seed and 24 from seed 116 (whose links
  once outgrew the test's arena for their bytes) for the vector passes over
  blocks of 16 atoms (the count's and the placement's) and 16 symbols (their
  references): atoms of every class a static executable takes, in runs of
  random lengths so that blocks change bucket at every lane, of sizes from 0,
  zero-fill ones among them, aligned up to a page; symbols in half of them,
  thread-local or not, weak ones defined nowhere, GOT slots for some, and a
  table pointing at every one; every other link's atom table no larger than
  its atoms, past the class gather's limit. Each is written by every lane
  count and by the exact twins into the same file, which runs;
- the patch kernel: the fast path against the checked one, on random fixups
  of every kind with wide addends, at every count from 0 to 199 from four
  starts, with the kind column ending at a `PROT_NONE` page.

`--seed N` changes the programs and the random inputs, and `--functions N`
the workers of the set named after it (2,000 by default).

## Dynamic PIE and shared library tests

`./build/compiler test-dynamic [--seed N] [--functions N] [--directory PATH]
[--cross-check] [--keep]` (about 1.54 s with `--cross-check` on the 9700X)
checks `executable.c`'s dynamic PIEs against this system's
runtime (`runtime_import`), in a directory of its own under `--directory`
(`TMPDIR` or `/tmp` by default), which it removes unless `--keep`. Each
program is bound by `bind.c` and written by groups of 1, 2, 3 and 8 lanes,
each time from zeroed fields over the last file scribbled over and every
other time over poisoned scratch memory: the files must be byte-identical.
Each file is read back and checked by the test's own rules: the headers and
`.dynamic` (lld's tags, `DT_INIT` and `DT_FINI` crti.o's functions, and
`.gnu.version_r`'s entries, as many as its `sh_info`, its chain and
`DT_VERNEEDNUM` say);
`.dynsym` (every dynamic symbol once, undefined, an import at its version,
before the exports, and each export at its address, found through
`.gnu.hash` as glibc's loader looks it up); `.gnu.hash`'s shape (lld's); and
every relocation the loader needs, from `.rela.dyn` and `.relr.dyn` decoded,
against the GOT's slots and the `ABS64` fields of the atoms the loader writes,
with each slot's bytes and each call's stub; the loads and calls through the
GOT, each that relaxes by test-executable's rule rewritten and each other
kept, with a GOT slot for each symbol that an unrelaxed GOT kind or a call to
a dynamic symbol reaches, and for no other, in symbol order; and the unwind
tables, by test-executable's rules (the runtime's FDEs among them). The link's
bytes are binding's again after the writes, for the checks and the objects
that lld links. The writer's exact twins must write the same file with 1 and 8
lanes, and a write without RELR must have the same relocations in `.rela.dyn`
alone and run alike. The checks:

- programs: test-link's two sets that call libc, and one of `--functions`
  workers (2,000 by default) whose table of pointers has targets that the
  writer's filter takes for symbols outside the image. Each must run under
  the system's loader, without stand-ins, and print what its model predicts;
- cases: test-bind's atexit and libgcc programs and linkers' cases, each bound or refused
  as GNU ld and lld link it, and run with their links' status; the loader's
  cases with `DT_NEEDED` the loader after `libc.so.6` exactly when a strong
  reference needs it, and `.gnu.version_r` an entry for each library, each
  import's version in its library's;
- general- and local-dynamic TLS (#515): test-bind's split-stack PIE, whose
  `generic-morestack.o` has such accesses, and assembly with each sequence
  through the PLT and the GOT, to its own thread-locals and to `libc.so.6`'s
  `errno` (status 53): every access's bytes in the file the relaxation's
  (local-exec, or initial-exec through the import's GOT slot), each written
  from binding's bytes (the relaxation can't be repeated), and run; calls of
  `__tls_get_addr` that no access makes, through its stub and its GOT slot
  with the executable's own module, beside an access whose call the
  relaxation consumes, written and run (status 120); and refused, by every
  lane count and the exact twins, without an output: that assembly written
  without `__tls_get_addr` named (at its first access to `errno`); and a
  local-dynamic access to `errno`, which `link_check` refuses
  (`LINK_ERROR_BINDING`);
- exports: sixteen functions whose names `libc.so.6` defines too, which
  binding exports and `main` finds with `dlsym` through the executable's
  `.gnu.hash`;
- imports: 256 of `libc.so.6`'s functions, each called through a stub and
  pointed to, which the loader must bind at their versions;
- refusals: a pointer to `main` in `.rodata` (a text relocation), an `ABS32` to
  `main` and a `PC32` to a dynamic symbol in `.data`, a `lea` of and a `PC64`
  in `.data` to a hidden weak reference to nothing, which stays at address 0
  wherever the program loads, and a `lea` of, a call to and a `PC64` in `.data`
  to an absolute symbol, which stays its value (GNU ld and lld refuse them
  too), each refused with its fixup by every lane count and by the exact twins,
  without an output; a call to and a pointer to a weak reference to nothing,
  and to a hidden one, loads through the GOT slots of the first and of the
  program's data (by `GOTPCREL`, which doesn't relax), the dynamic one's first
  in its word of the bitsets, and a load of the data that relaxes to a `lea`, a
  pointer to `main` at an odd address, which RELR can't hold, and pointers in
  `.data` and `.rodata` to, a load through the GOT of and an `ABS32` in `.data`
  to an absolute symbol at 42, which need no relocation (as lld links them),
  all of which run; and a lane short of scratch memory (lane 0 of 1 and of 8
  lanes, the last of 2 and of 8), with which every link that fails must fail
  with `EXECUTABLE_ERROR_MEMORY` and no output, and one with room enough must
  run.

With `--cross-check`, which needs `ld.lld`, `cc`, `objdump` and `readelf`, the
bound link's objects (the object writer's) are linked by lld as a PIE against
`libc.so.6` and the loader alone (with `--build-id`, and with `-z
pack-relative-relocs` when libc defines `GLIBC_ABI_DT_RELR`), and the two files
must have the same segments (the program headers' types in order, their flags
and the `PT_LOAD`s' alignments), the same dynamic symbols (name, binding, type,
whether defined, version), the same symbols with a GOT slot the loader fills,
as many GOT slots relocated RELATIVE and as many that no relocation fills (but
for ours of thread-locals' offsets, whose initial-exec accesses lld relaxes to
local-exec), the same `R_X86_64_64`s with their addends, as many RELATIVE
pointers outside the GOT, the same init and fini arrays' sizes, the same
`DT_NEEDED` libraries in order, each with the same needed versions, the same
`DT_INIT`, `DT_FINI`, `DT_GNU_HASH` and `DT_VERNEED`, and `.eh_frame_hdr`
tables (lld's with `--eh-frame-hdr`, as GCC's driver asks) that index the
same functions with the same ranges. And each program runs again with a
library preloaded, which `cc` builds: its `write` walks the stack with
libgcc's `_Unwind_Backtrace`, which finds each frame's FDE through
`PT_GNU_EH_FRAME` and the table, and the walk must go through `main` and end
at `_start`, the outermost frame. And `readelf -wF`'s reading of each of the
program's functions' FDEs (the CFA's rule and each saved register's, from each
location on) must be, row for row, what `objdump -d`'s disassembly of the
function's pushes and pops gives as GCC's CFI: the CFA 8 bytes further from
`rsp` after each push and 8 nearer after each pop, a callee-saved register a
push saves at the CFA less the new offset, and one a pop restores as it was.
`--seed N` changes the programs. The 9700X job's test step runs it with
`--cross-check`.

The same command also writes generated and compiled shared libraries against
`runtime_import_shared`. Every library is byte-identical on 1, 2, 3 and 8 lanes
and through the exact twins, then loads with `dlopen` and runs through the
generated harness. The file checks require ET_DYN without PT_INTERP, SONAME,
the library's exported symbols and initial-exec TLS, and the corresponding
dynamic relocations. Compiled fixtures verify that the executable's definitions
preempt the library's calls, data and pointer tables, and one whose 128-bit
division, remainder and conversions call seven of `libgcc.a`'s functions, whose
members join it, checks their results, and one whose code walks its stack with
`_Unwind_Backtrace`, which needs `libgcc_s.so.1` where the program that loads
it doesn't. Then the unwinder (#489, `dynamic_test_libgcc_s`): test-bind's
split-stack code as a library, and a PIE whose `main` calls
`_Unwind_Backtrace`, compiled by `cc`, bound with an import of
`libgcc_s.so.1`'s, written by every lane count with `DT_NEEDED`
`libgcc_s.so.1` before `libc.so.6` and its version need (the structure's
rules), and run, the library through a harness that doesn't need
`libgcc_s.so.1`. On a system without `libgcc_s.so.1`, binding must refuse
both instead (the library's as `BIND_ERROR_MEMBER_UNDEFINED`), and the
compiled library that walks its stack is left out; test-import, test-bind
and test-checker-driver assert the same fallback (`cc
-print-file-name=libgcc_s.so.1` tells the driver's test). With `--cross-check`, lld
links the same objects with `-shared`; the comparison accounts for its lazy PLT
slots and our non-lazy GLOB_DAT slots. Local-exec TLS, text relocations, a direct
address of a preemptible function, and currently general/local-dynamic TLS are
refused. See "The shared library" in the linker design for the supported scope.
Mapped-file checks hold the old library's descriptor and shared mapping across
replacement on every lane count; its inode and bytes must survive while new
opens find the new library. File-cleanup checks also preserve a concurrently
created destination when a first publication fails.

## Static executable tests

`./build/compiler test-static [--seed N] [--functions N] [--directory PATH]
[--cross-check] [--keep]` (about 2.9 s on the 9700X, and 3.2 s with
`--cross-check`) checks `executable.c`'s static executables with the C
runtime against this system's static runtime (`runtime_import_static`), then
its static PIEs against the static PIE's (`runtime_import_static_pie`), in a
directory of its own under `--directory` (`TMPDIR` or `/tmp` by default),
which it removes unless `--keep`. Each program is bound by `bind.c` and
written by groups of 1, 2, 3 and 8 lanes and by the writer's exact twins with
1 and 8 lanes, each time from the atoms' bytes as binding left them (the TLS
relaxation and the GOT relaxation rewrite them), with zeroed fields over the
last file scribbled over, every other time over poisoned scratch memory: the
files must be byte-identical. A link without general- or local-dynamic TLS
accesses is then written once more over the bytes the last write left, its
loads and calls relaxed already, and must come out the same.
Each file is read back and checked by the test's own rules:

- the headers: `_start` the entry, no `PT_INTERP`, a `PT_NOTE` with the start
  file's ABI note (`crt1.o`'s or `rcrt1.o`'s), and `PT_TLS` over `.tdata` and
  `.tbss`; a static executable an `ET_EXEC` without `PT_DYNAMIC`, and a static
  PIE an `ET_DYN` with `PT_DYNAMIC` over `.dynamic`, whose tags are lld's:
  `DT_FLAGS_1` PIE, no `DT_NEEDED` or versions, `.dynsym` with the null
  symbol alone, `.rela.dyn`'s address and size and `DT_RELACOUNT` its
  RELATIVE relocations, the arrays', and `DT_INIT` and `DT_FINI` `crti.o`'s
  functions;
- the IFUNCs: an `IRELATIVE` relocation for each stub's slot and for each
  IFUNC's GOT slot without a stub, its addend the IFUNC's resolver, in
  `.rela.iplt` (a static PIE's after its RELATIVE ones in `.rela.dyn`); each
  stub in `.iplt` a `jmp` through its own slot; an IFUNC with a stub at its
  stub's address for its calls, and everywhere when a fixup other than a call
  takes it (its GOT slot too), where one whose stub only calls reach reads the
  stub's slot through the GOT, as lld's reads its `.got.plt` entry; and a
  static executable's `__rela_iplt_start` and `__rela_iplt_end` at
  `.rela.iplt`'s bounds;
- a static PIE's RELATIVE relocations: one of each GOT slot that holds an
  address in the image (an IFUNC's stub among them, when it stands for the
  IFUNC's address) and one of each `ABS64` field of an atom the loader writes
  whose target is in the image, with the value as its addend, and no others:
  `.relr.dyn`'s (decoded, the field holding the addend) for the slots and the
  fields at even addresses in atoms aligned to 2 or more when `libc.a` takes
  `DT_RELR` (it defines `arc4random`, as glibc 2.36 and later do), and
  `.rela.dyn`'s for the others;
- every fixup's field: test-executable's kinds' values, with an IFUNC's stub
  for its address, a general- or local-dynamic access as the local-exec
  sequence it relaxes to, `DTPOFF32` as an offset from the thread pointer, an
  absolute symbol's value, and nothing for a call of `__tls_get_addr`, which
  the access consumed; each `CALL_GOT` and `LOAD_GOT` with the addend -4 to a
  symbol of the image's that isn't an IFUNC relaxed from its instruction in
  the link's bytes (the test checks it is the `call`, `jmp` or `REX.W` `mov`
  the kind says): `addr32 call`, `jmp` and a `nop`, or `lea`, and every other
  keeping its instruction; the GOT's slots in symbol order (but for the
  targets whose every slot read relaxed, and the IFUNCs whose stubs only
  calls reach), then the stubs'; and every name of the linker's where its
  section or segment says (`_DYNAMIC` at `.dynamic`);
- the init and fini arrays: their prioritized atoms first, by priority and in
  id order, then the others in id order, from their section's start to its
  end;
- the unwind tables, by test-executable's rules.

The checks:

- programs: test-link's two sets that call libc, and one of `--functions`
  workers (2,000 by default): each must run and print what its model
  predicts, as a static executable and as a static PIE;
- compiled: test-bind's C programs (hello, stdio, threads, jumps and weak-puts)
  and this file's: prioritized constructors and destructors beside libgcc's
  `cpuinfo.o`'s; decimal floating point, whose rounding mode and flags are
  libgcc's thread-local variables, reached by general-dynamic accesses, from a
  thread too; GCC 14's heap trampolines, whose member's accesses are
  local-dynamic, where `cc` is GCC 14 or later (it says so otherwise); and
  assembly with each general- and local-dynamic sequence through the PLT and
  the GOT (status 31), with a local IFUNC called, pointed to and compared, and
  a global one called through the GOT (status 53), with an IFUNC called
  directly, called through its GOT slot and loaded through it, whose stub only
  calls reach, so that the loads read the stub's slot (status 63), with a weak
  IFUNC defined nowhere, which has no resolver, loaded through the GOT, taken
  by a `lea` and pointed to, each 0 (status 0), and with absolute symbols that
  another object defines (combined with `ld -r`, since GNU as resolves a `lea`
  of an absolute symbol its file defines), taken by a `lea`, through the GOT,
  as an immediate and pointed to (status 0). `cc` compiles each, and each must
  run as `cc -static`'s link of the same object does, with the same status and
  output, both from directories of the same length (`argv[0]`). As static PIEs,
  each must run as `cc -static-pie`'s link does, but the IFUNC program, which
  must run with status 53, as lld's link does (GNU ld's gives the pointer in
  `.data` the IFUNC's implementation, where a `lea` gives its `.iplt` entry,
  and the comparison fails: 52), and the weak IFUNC's and the absolute symbols'
  programs, whose `lea` of address 0 and of an absolute symbol a static PIE
  refuses (`EXECUTABLE_ERROR_RELOCATION`, by every write), as `cc -static-pie`
  refuses them;
- refusals: assembly with a call of `__tls_get_addr` outside an access
  (refused naming the symbol), a general-dynamic access without its prefixes
  (its fixup), a pointer to an absolute symbol farther than int32 from the
  image's base (the symbol) and a `lea` of one whose offset from the base
  fits int32 but not its distance from the `lea` (its fixup, as GNU ld says
  the relocation is truncated; a static PIE refuses any `PC32` to an absolute
  symbol, as a relocation), each refused alike by every lane count and by
  the exact twins, without an output; as a static PIE, besides, a pointer to
  `main` in `.rodata` (a text relocation), an `ABS32` to `main` in `.data`
  and a `lea` of a weak reference to nothing, each refused as a relocation
  naming its fixup; and on the TLS program a lane short of
  scratch memory (lane 0 of 1 and of 8 lanes, the last of 2 and of 8), with
  which every write that fails must fail with `EXECUTABLE_ERROR_MEMORY` and
  no output, and one with room enough must run.

With `--cross-check`, which needs a `cc` that takes `-fuse-ld=lld`, each
compiled program is linked by `cc -static -fuse-ld=lld` too (and
`-static-pie`), which must run alike and have as many `IRELATIVE`
relocations, the same TLS segment's alignment and init and fini arrays of the
same sizes; but lld 23 links the weak IFUNC's program wrong (an `IRELATIVE`
relocation with resolver 0, which faults at startup), so that one only has to
link, and the static PIEs that a static PIE refuses aren't linked; lld packs
a static PIE's RELATIVE relocations into `.relr.dyn` (`-z
pack-relative-relocs`) as ours are, and a static PIE must have as many as
lld's. Then the GOT (`executable_test_got`, from the files' sections): our
slots must be as many as lld's `.got` entries and IFUNCs' `.got.plt` entries,
but for those of thread-locals' offsets, whose initial-exec accesses lld
relaxes to local-exec, and in a static executable those that only tests and
binops with REX read through the GOT, which lld writes with immediates
without PIC (`static_test_immediate_slots`: 5 of the decimal program's).
`--seed N` and
`--functions N` change the generated programs. The 9700X job's test step runs
it with `--cross-check`.

## Direct executable benchmark

`./build/compiler benchmark-executable [--directory PATH] [--functions LIST]
[--lanes N] [--seed N] [--only kernels|hash|links|dynamic|static|static-pie|shared]
[--tls control|general|local|imported] [--exact] [--after-scalar MS]` (about
6 s)
reports medians of:

- the patch kernel and the variants it was chosen over (a branch on the kind,
  the checked path, 64-bit targets, and gathers with scalar stores or
  scatters) on the design's three synthetic sizes, each variant's bytes
  checked against the kernel's first;
- the chunk hash: 256 KB in L2, whole and in pieces, and 64 MB;
- links of generated programs of 1, 64, 2,000 and 180,000 workers
  (`--functions` takes a list), written into `--directory` (`TMPDIR` or
  `/tmp`) by a group of `--lanes` lanes that runs every write of a case, as a
  compiler runs its phases on one group: in place and as a new file, phase by
  phase, against the floor, one `pwrite` of the same bytes; and with one lane,
  the patch kernel on each program's own mix of kinds, three ways: relaxing
  the loads and calls through the GOT from the code generator's instructions
  (put back before each run, outside the time), relaxing them over the forms
  the relaxation wrote, and without relaxing them; and the unwind tables'
  passes: the FDEs counted by the vector path and by the walk, their table
  entries both ways, and the table's sorted check and sort;
- the same sizes as programs that call libc, bound against the system's
  runtime and written as dynamic PIEs the same way, with their dynamic
  symbols, relocations, RELR words and stubs (`--only dynamic` runs these
  alone); then again with a probe per worker: a control of the
  general-dynamic probe's bytes, atoms and fixups without TLS (a `PC32` to a
  data object and a direct call), then TLS accesses that the writer relaxes
  (#515): a general-dynamic access to a thread-local of the program's
  (local-exec), a local-dynamic one with its `DTPOFF32`, and a general-dynamic
  access to `libc.so.6`'s `errno` (initial-exec through its GOT slot), each
  write from binding's bytes, put back outside the time, and every probe's
  bytes checked (`--tls control|general|local|imported` runs one mode alone),
  so that a mode's rows less the control's are what its relaxation costs;
- bound against its static runtime and written as static executables,
  with their stubs, IRELATIVE relocations and GOT slots (`--only static`);
- and bound against the static PIE's runtime and written as static PIEs,
  with their stubs, relocations (RELATIVE among them) and GOT slots
  (`--only static-pie`);
- bound against the shared-library runtime and written as libraries, including
  exports, preemptible calls and initial-exec TLS (`--only shared`, with
  `--tls general|local` a general- or local-dynamic probe per worker, which a
  library keeps and `dlopen` runs).

Executable writes after a case's first reuse the last one's file, whose loads
and calls through the GOT the writer relaxed already. Shared-library writes
replace their file each time. Run it under the 9700X's lock
with `taskset -c 2` for one lane, or with `taskset -c 0-7` for 8 (one CPU per
core), since the lanes' threads keep the affinity they start with.
`docs/object-writer-and-linker.md` ("Patching and writing", "The first
executables", "The dynamic PIE", "The static executable with libc", "The
static PIE", "GOT relaxation in every executable" and "The shared library") has
the numbers.
The default 9700X job runs it after the tests, pinned to one allowed CPU;
like the others, its measurements are informational.
