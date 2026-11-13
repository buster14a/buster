# foreign-driver

Run commands and resolve source paths from the repository root. Follow the
[shared agent rules](../../AGENTS.md), including the 9700X lock and `cores`
requirements. [Command index](README.md).

## Module overview

- `src/buster/compiler/foreign_driver.c` supplies `link-foreign`, a raw input
  command using the built-in resolver/converter/writer. The caller supplies
  startup objects and libraries in order, as GCC's driver hands them to GNU ld,
  whose `--build-id`, `--eh-frame-hdr`, `--hash-style=gnu`, `-m elf_x86_64`
  and `-z now`/`relro`/`noexecstack`/`text` it takes as what its output is
  already. It writes every kind (#394): static executables (with `libc.a` or
  without libc, `-static`), static PIEs (`-static -pie`), dynamic PIEs (`-pie`)
  and shared libraries (`-shared`, `-soname`), with copy relocations and their
  aliases, `__start_`/`__stop_`, `STB_GNU_UNIQUE`, lld's rule for libraries'
  own references, lld's regular symbol table, and a dynamic PIE's own IFUNCs
  through the writer's stubs and `IRELATIVE` relocations; a shared library
  refuses an IFUNC that isn't preemptible, and a static link refuses every
  IFUNC a fixup reaches unless its entry is glibc's startup, recognized by its
  files (the system's `crt1.o` or `rcrt1.o`, by identity, then `libc.a`'s
  `__libc_start_main`), which applies their `IRELATIVE` relocations, or
  `--startup-applies-irelative` says the program's own does. It keeps the
  inputs' DWARF (`foreign_debug.c`), which `-S` and `-s` leave out. It takes
  GCC's `collect2` LTO plugin options (`-plugin` for `liblto_plugin.so` or
  `LLVMgold.so`, and `-plugin-opt=...`) when no input needs the plugin, and
  refuses a direct object or loaded archive member with a `.gnu.lto_*` or
  `.llvm.lto` section, or raw LLVM bitcode, by input, member and section
  (#816). GC, literal pooling and CIE pooling are off by default.
  It runs on the lanes its objects pay for (#594, `foreign_driver_lanes`): one,
  and one more per 3 MB of its objects, at most 8 and one per core of the CPUs
  it may run on, which lane 0 decides by opening the command line's first inputs
  (`foreign_open_first`), so that a link of a few objects runs on one lane; or
  with `--lanes N` on N, at most one per CPU. Its session (#905, `lane.c`)
  creates the most lanes it may take once, after its arguments and before any
  input is opened, none for `--lanes 1` or one CPU, its lane 1 counting the
  cores without `--lanes` (`BUSTER_LANE_SESSION_PER_CORE`), and runs it on one
  lane if they can't start, `--lanes` or not.
  On more than one lane on the vector path, the conversion
  (`foreign_link.c`) runs its global names, objects' maps, relocations and
  `link_check_lanes` (`link.c`) on that many lanes (`foreign_link_lanes.c`,
  #87), into the exact path's link; the resolution, the conversion and the
  write are rounds of the link's flow (a `LaneFlow`) on the session's lanes,
  which keep their scratch arenas from one to the next (#501), and `--time` reports
  the lanes, the session's start, the first inputs' opening with their
  objects' bytes and the resolution's, the conversion's and the writer's
  phases.
  [The command contract](../../docs/foreign-driver.md) has the details. Its tests
  (`foreign_driver*_test.c`, `foreign_copy_test.c`, `foreign_bounds_test.c`)
  run through `test-foreign --cross-check`, and `test-foreign-corpus`
  (`foreign_corpus_test.c`) compares it with lld and mold on a corpus
  ([Foreign corpus test](foreign-corpus.md#foreign-corpus-test)).
  Static inputs may define `__tls_get_addr` as an ordinary function: its
  calls, address, GOT slot and unwind/debug references are preserved while
  general- and local-dynamic TLS accesses still relax. The driver tests cover
  definition-only, ordinary-call and mixed TLS-access links, with and without
  debug information, on both paths and all lane counts, against GNU ld and lld.
