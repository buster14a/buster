# foreign

Run commands and resolve source paths from the repository root. Follow the
[shared agent rules](../../AGENTS.md), including the 9700X lock and `cores`
requirements. [Command index](README.md).

## Module overview

- `src/buster/compiler/foreign.c` is the first part of linking other
  compilers' objects (#86, steps 1 to 5 of the design's "Linking other
  compilers' objects"): a command line of ELF objects, archives (thin ones
  too), shared libraries and `libc.so`'s script subset opened across lanes,
  each archive's symbol index read alone (`import_archive_lazy`; a member's
  header when it loads, `import_archive_member`), each round's objects'
  headers and global symbols parsed into columns of the round's own, their
  records exchanged over fixed hash partitions into each partition's tables,
  archive members loaded in frontier rounds (for the needed shared libraries'
  references too), and COMDAT groups picked by signature; GNU ld's results
  where they don't depend on the archives' order, lld's where they do. Each
  phase has a vector path and an exact twin.
  `foreign_test.c` holds its tests (`main.c` includes both;
  `./build/compiler test-foreign` runs them) and `foreign_benchmark.c`
  provides `benchmark-foreign`.
- `ImportObject.executable_stack_section` preserves the first exact
  `.note.GNU-stack` section requesting `SHF_EXECINSTR` in the existing scalar
  and vector section passes. Foreign conversion refuses a selected requesting
  object before the NX-only executable writer is reached; unextracted archive
  members do not contribute. `test-import` covers metadata and retry parity;
  `foreign_stack_test.c`, included by `test-foreign`, covers original
  direct/archive/COMDAT selection and diagnostics at 1/2/3/8 lanes. The linker
  guide's "Explicit executable-stack requests in foreign objects" records
  the conservative grouped/excluded-marker policy and the boundaries of these
  producer tests.
## Foreign tests

`./build/compiler test-foreign [--seed N] [--functions N] [--directory PATH]
[--cross-check] [--keep]` (about 2.4 s on the 9700X, and 2.9 s with
`--cross-check`, 1.7 s of each the slow lanes' pauses below) checks
`foreign.c`. Every resolution runs by both paths and with groups of 1, 2, 3
and 8 lanes, with partitions of about 16 records
(`ForeignCommand.partition_records`) so that the cases' few records spread
over every lane, which must describe it byte for byte alike: the loaded
objects, every name's resolution, winner and counts, every symbol's name id
and every COMDAT group's fate.

- programs: `test-link`'s seven sets, each unit's object written by the object
  writer, resolved four ways: every unit an object; the units after the first
  in an archive after it; split between an archive and a thin archive; and an
  archive before the first unit, which only lld's back references load. Every
  name must be defined, but the linker's (`runtime_linker_name`), the weak
  references to nothing and, for the sets that call libc, libc's, which bind
  to `libc.so.6` through the system's `libc.so` script, with the crt objects
  and libgcc where GCC's driver puts them;
- compiled programs: C sources `cc` compiles (and C++ that `g++` compiles at
  `-O0`, for COMDAT groups of inline functions and templates), resolved as
  cases with what each must give (`foreign_test_outcomes`): commons of two
  sizes beside strong and weak definitions; GNU ld's common rule (a common
  symbol loads the member that defines its name as global data, not one that
  defines it as a function, weakly or as another common); weak and strong
  definitions; a name two archives define, in both orders, a member that needs
  an earlier member of its own archive, a thin archive, `--whole-archive` and
  `-u`; back references; a shared library beside an archive in all five orders
  with the object; a duplicate; undefined names; a PIE linked with libc as
  GCC's driver links one; COMDAT groups in objects and archive members,
  among them a member loaded for a later one; a strong reference before an
  archive whose member defines the name and an object after it that defines
  it too, weakly (the member loads and its definition wins) and strongly (a
  duplicate); a shared library that refers to a name an archive after it
  defines, needed and under `--as-needed` (the member loads), and under
  `--as-needed` with nothing needing it (nothing loads); and a thin archive
  that holds another archive's member (GNU ar's `/N:M`);
- with `--cross-check`, which needs `ld.bfd`, `ld.lld`, `cc` and (for the C++
  cases) `g++`: each program and case linked by GNU ld (`--trace` twice, `-Map`
  and `-y`) and by lld (`--why-extract`, `-Map` and `-y`). The members each
  loads (GNU ld names a thin archive's by its path), each winner (its symbol's
  address in the output's `.symtab` within the map's input section of the
  object and section the resolution chose; TLS symbols from `PT_TLS`'s
  address; not symbols of `SHF_MERGE` sections, which linkers merge), each
  group kept exactly when its first section is in the map, and the `-y` lines
  about loaded objects (GNU ld's, every one, for names in two objects or more
  but the linker's; lld's, each one, since it prints a definition only when it
  takes the name over) must be the resolution's, and a link fails exactly when
  the resolution leaves a name undefined or defines one twice. Where a case
  says a linker differs as `foreign.c` documents (GNU ld with back references
  and an archive before a shared library and its object; lld with GNU ld's
  common rule, an as-needed library nothing needs, whose references it
  counts, and a thin archive of another archive's member, which lld 23
  refuses; both for a member loaded for a later one, whose COMDAT group they
  keep), its differences are counted apart and printed as a count;
- malformed inputs, each refused naming its place (the failure's error, input
  and member, and the reader's error, section, symbol, type and offset): a
  compiled object with one field changed (a symbol's binding, type, section
  index, common alignment or name; the symbol table's first global; a
  relocation section's link, target or type), a broken archive member (refused
  only once it loads), a thin archive's missing member, a member that isn't an
  object, a script that doesn't parse, a `-l` name no directory has, a missing
  file, a FIFO (refused, not waited on), an input of four bytes (the ELF
  magic alone) and an unknown option. Each malformed object and each file
  that fails is resolved again on 2 and 3 lanes, each lane in turn pausing
  2 ms at every decision to go on (`ForeignCommand.slow_lane`), so that the
  others go on to the next phase and fail there while it decides: the
  failure must be the same, and a watchdog fails the test if the lanes wait
  for each other for 60 s. Then a group that can't start (in a child whose
  `RLIMIT_NPROC` leaves no room for a thread, which doesn't bind root) must
  fail as `FOREIGN_ERROR_LANES`, with CPUs given and without;
- page edges: inputs given in memory (`ForeignArgument.bytes`) ending at a
  `PROT_NONE` page: every prefix of an object and of an archive, and the object
  with each byte set to 0, 0xff and a random value, each resolved by both paths
  alike or refused alike; a read past the end faults;
- extended sections: an object made by hand of 65,527 sections, its global
  symbols in sections 65522 and 65521 through `SHN_XINDEX` (the values of
  `SHN_COMMON` and `SHN_ABS`) beside an absolute and a common one: each must
  be what its `st_shndx` says, by both paths, and with a second object that
  defines the first strongly too, a duplicate.

The files go in a new directory under `--directory` (`TMPDIR` or `/tmp`),
removed unless `--keep`. `--seed N` changes the generated programs, and
`--functions N` the workers of the set that takes them. The default 9700X job
runs it with `--cross-check`.

## Foreign benchmark

`./build/compiler benchmark-foreign [--functions LIST] [--filler N] [--seed N]
[--lanes LIST] [--cpus LIST] [--runs N] [--directory PATH] [--kinds LIST]
[--linkers] [--keep]` (about 9.3 s on the 9700X with `--linkers`) resolves
`link_program.c`'s programs that call libc, written as objects, 200 workers
per unit: one worker, 2,000, and synth's shape (102,400 workers in 512 units,
up to 64 filler instructions each), or with `--functions` a program of each
count. Each is resolved as GCC's driver links it (`--kinds`: `dynamic`, a PIE
against `libc.so`'s script, libgcc and the crt objects; `static`, against
`libgcc.a`, `libgcc_eh.a` and `libc.a` in a group, whose members the rounds
load; and `members`, the dynamic one with every unit but the first in an
archive), by each path and each lane count of `--lanes` (1 by default), the
lanes pinned to `--cpus` in order. It reports what the resolution did (its
inputs, objects, members, rounds, global symbols, groups, names and
partitions) and medians of `--runs` runs (21) after a warm-up: the whole
resolution and each phase (open, indices, headers, symbols, exchange, resolve,
load, groups and winners), the slowest lane's. With `--linkers`, mold
(`--perf`) and wild (`--time`) link the same command lines on one thread, 11
runs after a warm-up, and the medians of their phases that do the same work
are printed: mold's `read_input_files` and `resolve_symbols`, wild's opening,
loading, resolving and section resolution. Run it under the 9700X's lock with
`taskset -c 2` for one lane, or with `--cpus` for several.
`docs/object-writer-and-linker.md` ("Steps 1 to 5, as built") has the numbers.
The default 9700X job runs it after benchmark-snapshot, pinned to one allowed
CPU: with `--linkers` on the daily and dispatched runs and on pull requests
that change `foreign_benchmark.c`, since mold's and wild's times don't depend
on a pull request, and without it on other pull requests. Like the others,
its measurements are informational.
