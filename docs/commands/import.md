# import

Run commands and resolve source paths from the repository root. Follow the
[shared agent rules](../../AGENTS.md), including the 9700X lock and `cores`
requirements. [Command index](README.md).

## Module overview

- `src/buster/compiler/import.c` holds the import path's readers: ELF
  relocatable objects into the representation, archives' members and symbol
  index, shared libraries' exports at their default versions (indexed by name
  hash), version definitions and references, and the commands of the linker
  scripts that `libc.so` and GCC's `_asneeded` libraries are. The object,
  archive and library readers each have a vector path and an exact one, which
  takes what the vector path leaves to it (the design's "Step 5's phases,
  data-parallel"). `runtime.c` finds the C runtime's files as GCC's driver
  does (`Scrt1.o`, `crti.o`, `crtbeginS.o`, `crtendS.o` and `crtn.o`, GCC's
  `libgcc.a`, which the driver names around `libc.so`, `libgcc_s.so.1`, which
  it names as needed after each `libgcc.a`, where the system has one, and the
  `libc.so.6`, `libc_nonshared.a` and loader that `libc.so` names, the loader
  as needed) and imports them into a link of their own, with what each name
  they refer to binds to (the first part of step 5 of the design's plan), each
  object's records as a piece, and an index and a filter of their global
  names and the libraries' (the loader's version indices numbered after
  `libc.so.6`'s, and `libgcc_s.so.1`'s after both);
  `runtime_import_static` does the same for a static executable's runtime
  (`crt1.o`, `crtbeginT.o` and `crtend.o`, and the archives of GCC's group:
  `libgcc.a`, `libgcc_eh.a`, `libatomic.a` where GCC has it, and `libc.a`),
  with each member's strong references and COMDAT groups (step 6's first
  part), `runtime_import_static_pie` for a static PIE's (`rcrt1.o`,
  `crtbeginS.o` and `crtendS.o` with the same archives; step 6's fifth part),
  and `runtime_import_shared` for a shared library's (the dynamic runtime
  without `Scrt1.o`, its unit and piece empty).
  The object reader gives `GOTPCRELX` and `REX_GOTPCRELX` relocations the
  kinds a linker may rewrite (`CALL_GOT` and `LOAD_GOT`) only on the
  instructions those kinds are (`call` or `jmp` through the slot, a `mov`
  from it with `REX.W`), and `GOTPCREL` on any other.
  `import_test.c` holds their tests (`main.c` includes it; `./build/compiler
  test-import` runs them) and `import_benchmark.c` provides
  `benchmark-import`.
## Import tests

`./build/compiler test-import [--seed N] [--cross-check]` (about 0.21 s on
the 9700X, and 0.32 s with `--cross-check`) checks `import.c` and
`runtime.c`:

- round trips: each unit's object of `test-link`'s seven programs (with 500
  workers in the one that takes `--functions`), with and without function
  sections, and the object writer's unit of 33,000 functions in sections of
  their own (past `SHN_LORESERVE` sections), imported into a link of its own.
  Each of the unit's atoms must be in the atom its section became, byte for
  byte where the writer put it, each symbol at the same place with the same
  name, flags and size, and each fixup at its place, of its kind, pointing at
  the same place with the same addend, and no other fixups;
- an object made by hand with a section of every class, the sections the
  import drops (`.sframe` with its relocations, a note, `.note.GNU-stack` and
  `.comment`), every kind of symbol (an IFUNC and absolute ones among them)
  and every relocation type the representation has, out of order, with
  `R_X86_64_NONE` and addends past int32, a COMDAT group and prioritized init
  and fini arrays: each section, symbol, fixup, group and priority imported
  exactly, the atoms' bytes in place in the file; among them `GOTPCRELX` and
  `REX_GOTPCRELX` relocations on a `call`, a `jmp` and a `mov` through the GOT,
  which import as `CALL_GOT` and `LOAD_GOT`, and on a `push`, an `add` and a
  `mov` whose addend isn't -4, which import as `GOTPCREL`; the same with its
  relocations in order, with `e_shnum` and `e_shstrndx` in section 0, with
  `.eh_frame` as `SHT_PROGBITS` and with tables' `sh_entsize` 0; and one
  without a symbol table, as `crtn.o` is. Then each check a well-formed object
  never trips, one change at a time, with the error, the index of the
  section, symbol or relocation, the type and the byte offset it reports;
- archives made by hand (a 32-bit index, a 64-bit one and none, long names,
  an odd member's padding, a member of no bytes, a member without a symbol,
  an index of several of the vector path's batches with a member's symbols
  across them, and no members), then malformed ones;
- a shared library made by hand: version definitions, a name at a default
  version and at a hidden one (kept apart, with its version's index), IFUNC,
  `GNU_UNIQUE`, `COMMON`, TLS and absolute symbols, weak and protected ones,
  hidden and local ones and one at `VER_NDX_LOCAL`, which aren't exported,
  references (with their versions' indices), and a name exported twice (the
  index finds the first); the same without `.gnu.version`; then malformed ones;
- linker scripts: `libc.so`'s and GCC's `_asneeded` ones, commas, comments
  and `OUTPUT_FORMAT`, then each error at its offset;
- page edges: every prefix of each input made by hand (the object also with
  its relocations in order), and the input with each byte set to 0, 0xff and
  a random value, ending at a `PROT_NONE` page, through every reader: each
  succeeds, or fails reporting a place within its input. A read past the end
  faults; the command then names the input and exits with status 1;
- the paths: each object, archive and library above (the round trips, the
  inputs made by hand, every malformed one, every page-edge input and this
  system's runtime) is read by its reader's vector path, its exact path and
  the reader itself, each into a link (or a read) of its own. The reader's
  result and records must be the exact path's; the vector path may take an
  input only when the exact path reads it without an error, and must then
  make the same records, column by column. Each well-formed input must take
  the vector path, which sorts relocations out of order, and each check
  prints how many inputs each path took. A library's index must find what a
  linear scan of its exports finds;
- discovery: GCC's search under a directory of the test's own, whose newer
  version's directory has no `crtbeginS.o`: the search picks the older
  version and probes the newer one's `crtbeginS.o` too, so that installing it
  leaves the probes stale (`runtime_probes_current`) and a fresh search picks
  it;
- the runtime: `runtime_import` on this system's files. Each crt object must
  import with what it is for (`_start`, `_init` and `_fini` in `.init` and
  `.fini` atoms, crti.o's fragment first, `__dso_handle`, `__TMC_END__`),
  `libc.so.6` with its soname, `write`, `strlen`, `environ` and
  `__errno_location` at `GLIBC_2.2.5`, `__libc_start_main` at `GLIBC_2.34`
  and `GLIBC_ABI_DT_RELR`, the loader with its soname, `__tls_get_addr` at
  `GLIBC_2.3` and `_r_debug` and `__libc_stack_end` at `GLIBC_2.2.5`, its
  version indices past `libc.so.6`'s, `libgcc_s.so.1` with its soname,
  `_Unwind_Resume` and `__udivti3` at `GCC_3.0`, `_Unwind_Backtrace` at
  `GCC_3.3` and `__gcc_personality_v0` at `GCC_3.3.1`, its version indices
  past both libraries' and none of its exports one of theirs (binding takes
  theirs first, where the driver's order has `libgcc_s.so.1` first),
  `libgcc.a` then `libc_nonshared.a`, `libc_nonshared.a` with `atexit` in its
  index, `__udivti3` a member's of `libgcc.a` and `libgcc_s.so.1`'s export
  after it, `_Unwind_Backtrace` `libgcc_s.so.1`'s alone, and none of
  `libgcc.a`'s names an export of `libc.so.6` or the loader (binding takes
  an export before a member, which the driver's order, `libgcc.a` first,
  agrees with only so), and every reference to each name bound: `main` to the program,
  `__libc_start_main` and `__cxa_finalize` to libc, `__gmon_start__` and the
  `_ITM_` functions to nothing, `__TMC_END__` to `crtendS.o`, and
  `_GLOBAL_OFFSET_TABLE_`, which the crt objects that use the GOT name, to the
  linker. Each object's piece must follow the one before, with its atoms in
  its own unit and its names' bytes one run, and the global names (binding's
  index of the runtime's names and libc's) must hold each name once, found by
  its bytes and in its filter, each global symbol's its own: `main` defined by
  none of them, `__libc_start_main` libc.so.6's, `_r_debug` the loader's,
  numbered after `libc.so.6`'s exports, `__dso_handle` crtbeginS.o's, `atexit`
  a member's, and `_GLOBAL_OFFSET_TABLE_` and the init and fini arrays' bounds
  the linker's, whether or not the runtime refers to them;
- the static runtime: `runtime_import_static` on this system's files.
  `crt1.o`, `crtbeginT.o` and `crtend.o` with `_start`, `_init`,
  `__dso_handle` and `__TMC_END__`, and `crt1.o`'s ABI note; the archives in
  GCC's group order, each one's members one after another; `main` bound to
  the program, `__libc_start_main` to `libc.a`'s member,
  `__gmon_start__` to nothing, `__TMC_END__` to `crtend.o`,
  `_dl_relocate_static_pie` to `crt1.o`'s empty one, and
  `_GLOBAL_OFFSET_TABLE_` and the IRELATIVE relocations' bounds to the
  linker, and nothing to a shared library; the names its linker defines
  among the global names, the linker's, and no other; `memcpy` an IFUNC of
  `libc.a`'s, `_nl_current_LC_CTYPE_used` absolute, and `libgcc.a`'s
  priorities; each object's COMDAT groups as its `ImportObject` has them,
  with the same signature numbers for groups of the same signature and
  different ones otherwise; each piece's strong references as a scan of its
  symbols finds them, with the member each loads, and each global name's
  defining member; `link_check` on the whole runtime; and every crt object
  and member taking the vector path;
- the static PIE's runtime: `runtime_import_static_pie`, checked as the
  static runtime is: `rcrt1.o`, `crtbeginS.o` and `crtendS.o`, with the
  same archives; `_dl_relocate_static_pie` bound to `libc.a`'s
  `dl-reloc-static-pie.o`, which relocates the executable, and its
  `_DYNAMIC` to the linker; the IRELATIVE relocations' bounds, which
  `libc.a` refers to weakly and a static PIE's linker doesn't define, to
  nothing; and with `--cross-check`, every relocation compared with
  readelf's, as the static runtime's are;
- the shared library's runtime: `runtime_import_shared`, without a start
  object (its piece empty, no ABI note); `crti.o`, `crtbeginS.o`, `crtendS.o`
  and `crtn.o`; `libgcc.a`, `libgcc_s.so.1`, `libc.so.6`, the loader and
  `libc_nonshared.a`; nothing referring to `main`, which is no global name,
  only `libgcc.a`'s members referring to names nothing in the runtime
  defines, and `morestack.o`'s `_Unwind_Resume` bound to `libgcc_s.so.1`;
  `_init` crti.o's and
  `__dso_handle` crtbeginS.o's; the names its linker defines (`_DYNAMIC`
  among them), and no other; and every crt object and member taking the
  vector path.

`--cross-check`, which needs `readelf`, compares `libc.so.6`'s, the
loader's and `libgcc_s.so.1`'s exports (name, version, type, binding and
size) and references with
`readelf -W --dyn-syms`, and every relocation of the crt objects and the
archives' members, the static runtime's among them, with `readelf -W -r`:
each one must be a fixup of its kind at its offset (a `GOTPCRELX` or
`REX_GOTPCRELX` one's by the instruction before it, as the test reads it: 379
of `libgcc.a`'s are `GOTPCREL` on the 9700X, adds of slots), against the
symbol (or the section) readelf names, with its addend, and there must be no
other fixups. `--seed N` changes the programs
and the random bytes. The 9700X job's test step runs it with `--cross-check`.

## Import benchmark

`./build/compiler benchmark-import [--units N] [--functions N] [--seed N]
[--library PATH]... [--archive PATH]...` checks that each reader's vector
path reads its inputs as the exact path does, then reports 21-run medians of
both, the paths taking turns (time, cycles, instructions, IPC):

- objects: the object writer's objects of a generated program of `--units`
  units (64) of `--functions` workers (200), each about one of synth's
  translation units; the same with function sections; one of 33,000
  functions in sections of their own; this system's crt objects and
  `libc_nonshared.a`'s members; and a static executable's crt objects and
  archives' members, per symbol table entry and relocation;
- `libc.so.6` and each `--library` (per export and reference), with its
  exports' index against the open-addressing table it replaced, built and
  looked up for every name; `libc_nonshared.a` and each `--archive` (per
  index entry); `libc.so`'s script, which has no vector path;
  `runtime_import` end to end; the static runtime's archives; and
  `runtime_import_static` and `runtime_import_static_pie` end to end (a
  static PIE's runtime is the static executable's archives with other crt
  objects, so the archives' rows are its too).

Run it under the 9700X's lock with `taskset -c 2`. With `--library
/usr/lib/libstdc++.so.6 --library /usr/lib/libLLVM.so.23.1 --archive
/usr/lib/libc.a` it measures the larger files the design's "Step 5's phases,
data-parallel" has the numbers for. The default 9700X job runs it after the
tests, pinned to one allowed CPU; like the others, its measurements are
informational.
