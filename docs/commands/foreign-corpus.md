# foreign-corpus

Run commands and resolve source paths from the repository root. Follow the
[shared agent rules](../../AGENTS.md), including the 9700X lock and `cores`
requirements. [Command index](README.md).

## Foreign corpus test

`./build/compiler test-foreign-corpus [--sqlite DIR] [--lua DIR] [--self DIR]
[--compilers LIST] [--linkers LIST] [--directory PATH] [--keep]` is #86's
differential test of `link-foreign`, and #394's. Each program of its corpus is
compiled by each compiler of `--compilers` (gcc and clang) at -O2, their
default PIEs, then linked from one command line by `link-foreign` (this
executable, as a process of its own), by lld and by mold (`--linkers`), with
GCC's crt objects and libraries around the program's objects and archives, as
each kind of output GCC's driver links: a dynamic PIE, a static executable
(`crt1.o`, `crtbeginT.o` and `--start-group -lgcc -lgcc_eh -lc --end-group`),
a static PIE (`-static -pie --no-dynamic-linker -z text`, `rcrt1.o`) and a
shared library (`-shared -soname libcorpus.so`) of the program's library part,
compiled again with `-fPIC`, which a PIE of the rest that lld links against
lld's library loads, from each linker's directory with that linker's library.
The three outputs must:

- run alike: each run's exit status, output and errors, from directories of
  the same name, with the same arguments, input and environment;
- have the same `.symtab` symbols with the same sizes: each named entry's name,
  binding, type, visibility, size and whether it is defined or absolute, as a
  multiset, but for `STT_FILE` and `STT_SECTION` entries, the names each linker
  defines for itself (`foreign_corpus_synthesized`), mold's `$` labels and
  `__abi_tag`, which the crt object's ABI note holds and `link-foreign` writes
  as a note of its own;
- agree on the dynamic section (`DT_NEEDED` in order, `DT_SONAME`, the PIE
  flag, `DT_INIT`, `DT_FINI` and the arrays' sizes), on `.dynsym`'s names at
  their versions, and on the dynamic relocations by symbol and class (a GOT
  slot, `GLOB_DAT` or `JUMP_SLOT`; `COPY`; `R_X86_64_64`; `TPOFF64`;
  `DTPMOD64`; `DTPOFF64`; `IRELATIVE`, in `.rela.iplt` too, where a static
  executable of `link-foreign`'s has them as GNU ld does, each by its
  resolver's name and whether it writes a GOT slot or a pointer in data, and
  each writing a place of its own), with as many relative ones as lld's. A
  resolver's name is the first of the names the image's `.symtab` gives its
  address that the other image gives one of its resolvers too, since lld
  moves an IFUNC whose address is taken to its stub, and so does mold every
  IFUNC of a static executable. The comparison reads static executables
  (`ET_EXEC`) too, and fails an output it can't read.

And `link-foreign`'s program must come out byte for byte the same when it links
on eight lanes (`--lanes=8`), through the resolution's, the conversion's and
the writer's lane paths, as on one.

What a reference linker writes as its own valid choice is classified, counted
and printed as documented ("differs, as documented"), never dropped silently
(`foreign_corpus_reason`): mold leaves weak undefined names without a version
out of `.dynsym`, keeps COMDAT groups' signatures as absolute locals, drops
`__FRAME_END__` (and `crtbeginT.o`'s `__EH_FRAME_BEGIN__`), binds a copy's
aliases weakly, writes internal visibility as hidden and the
`__start_`/`__stop_` names local (and defines them for every section named as
a C identifier, though nothing refers to them), and keeps a few GOT slots that
lld relaxes; in a static executable and a static PIE it writes a hidden
symbol made local with default visibility, in a static executable an IFUNC
as a function at its stub, and in the PIEs a pointer to an IFUNC outside the
GOT with an `IRELATIVE` relocation of its own, where `link-foreign` and lld
relocate it relative to the IFUNC's stub (each such row of mold's must be one
of `link-foreign`'s stub pointers, one for one, by the IFUNC's resolver);
lld leaves an `R_X86_64_64` to a library's data that it scanned before the
reference that copies it symbolic, gives a function that code both calls and
loads from the GOT a second slot, and writes an IFUNC whose address is taken
as a function of no size at its stub; both keep the symbols of `libc.a`'s
`.gnu.warning` sections, which `link-foreign` leaves out with them. Any other
row one side has alone fails the test, and the first 12 are printed.

The corpus is two original programs, always: C, of three translation units
and an archive (two members that load, one that doesn't), with threads,
thread-locals, libc's and libm's data and functions, constructors and
destructors, a weak reference, a `__start_`/`__stop_` registry and two
IFUNCs (an `ifunc` attribute's and `target_clones`'), called, loaded from the
GOT and pointed to from data, whose pointers must compare equal; and C++,
of two translation units, with iostream, exceptions across them, RTTI,
templates in COMDAT groups, inline variables and a thread. With `--sqlite DIR`
the SQLite amalgamation's shell (`sqlite3.c` and `shell.c`), with `--lua DIR`
Lua's interpreter (its library as an archive) and with `--self DIR` this
repository's compiler, built by clang alone, join it, each with runs of its
own; the library parts are the C program's three archived units and its
counter, the C++ program's thrower, SQLite's `sqlite3.c` and Lua's library
(this compiler has none, and isn't linked as a library). The files go in a
directory under `--directory` (`TMPDIR` or `/tmp`), kept with `--keep` or on
a failure. The 9700X job's test step runs the original programs.
