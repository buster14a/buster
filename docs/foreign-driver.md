# Raw foreign linker command

`build/compiler link-foreign` links other compilers' objects: it connects the
foreign input resolver, conversion and optional transformations to the
built-in executable writer. It takes raw linker inputs, as GCC's and clang's
drivers hand them to GNU ld; callers supply startup objects and libraries in
link order. It does not invoke a system linker to produce its output.

```text
build/compiler link-foreign [-o OUTPUT] [-e ENTRY] [-static] [-pie] [-shared]
    [-soname NAME] [--dynamic-linker PATH] [--no-dynamic-linker]
    [--lanes N] [--scalar] [-s|--strip-all] [-S|--strip-debug] [--time]
    [--gc-sections|--no-gc-sections]
    [--merge-strings|--no-merge-strings]
    [--dedup-unwind|--no-dedup-unwind] [--startup-applies-irelative]
    INPUT... [-L DIR] [-l LIB] [-u NAME]
```

Defaults are a static executable named `a.out`, entry `_start`, one lane,
and all optional transformations disabled. `-pie` makes a dynamic PIE,
`-static` with `-pie` a static PIE (as GNU ld and lld read the two together;
GCC's `-static-pie` line is `-static -pie --no-dynamic-linker -z text`), and
`-shared` a shared library, which has no entry point unless `-e` names one,
with `-soname NAME` (or GNU ld's `-h NAME`, `--soname=NAME`) for its
`DT_SONAME`. `-shared` with `-static` or `-pie`, and `--no-dynamic-linker`
without `-static -pie`, are refused. Named regular symbols and the inputs'
DWARF are emitted by default (below, "Debug information"); `-s` and
`--strip-all` omit `.symtab`, `.strtab` and the DWARF while preserving the
dynamic symbol table and runtime dependencies, and `-S` and `--strip-debug`
omit the DWARF alone. `--scalar` selects the exact resolver, symbol-selection,
conversion and writer paths. `--lanes N` runs the resolution,
the conversion and the writer on up to N lanes, at most one per CPU the process
may run on: lanes past those share CPUs, and each of their syncs waits for a
lane the scheduler has set aside. `--time` reports, on standard
error after the link, each stage's wall time (the arguments, the interpreter's
discovery, the resolution with each of its phases on the slowest lane, the
conversion, the optional passes, the write with each of the writer's phases on
lane 0, and the release), as wild's `--time` and mold's `--perf` report
theirs. `--startup-applies-irelative` says that a static link's own startup
applies its `IRELATIVE` relocations (below, "What a static executable and a
static PIE hold"). `--help` describes the supported
command forms. The entry is a forced undefined reference during resolution,
so an archive can supply it; final entry lookup is repeated after symbol
remapping.

The options GCC's driver gives GNU ld that describe what every output of this
command is already are taken and change nothing: `--build-id`,
`--eh-frame-hdr`, `--hash-style=gnu`, `-m elf_x86_64` (and `-melf_x86_64`),
and `-z now`, `-z relro`, `-z noexecstack` and `-z text` (the writer never
writes a relocation in read-only memory). `-dynamic-linker` is
`--dynamic-linker`. Any other `-m` or `-z` keyword is refused, naming it. So
a driver's command line, as `gcc -###` prints it for GNU ld, links as it is:
`-pie` for a PIE, `-static`, `-static -pie` and `-shared` for the others.

## Input order and output boundary

Driver options are removed without reordering paths, `-L`, `-l`, `-u`,
`--undefined`, archive groups, whole-archive, as-needed or library-search
controls. The static kinds (a static executable and a static PIE) start
library search with archives only; a PIE and a shared library start with the
resolver's ordinary DSO preference. Explicit ordered `-Bstatic`/`-Bdynamic`
override that initial choice. An explicit `-static` also stays in the input
stream, preserving its existing search effect. Static output that selects a
needed DSO reports the incompatible input.

Every kind accepts explicit C runtime startup objects, the program objects,
libraries and closing CRT objects in the caller's chosen order: `crt1.o`,
`crti.o`, `crtbeginT.o` and the archive group `-lgcc -lgcc_eh -lc` with
`crtend.o` and `crtn.o` for a static executable, `rcrt1.o`, `crtbeginS.o` and
`crtendS.o` with the same group for a static PIE, and `crti.o`, `crtbeginS.o`,
`crtendS.o` and `crtn.o` around a shared library's libraries, as GCC's driver
gives them. A static program without libc links too. `--dynamic-linker PATH`
supplies a dynamic PIE's interpreter directly; otherwise the command finds the
system interpreter as [the interpreter note](foreign-interpreter.md)
describes. Neither form inserts startup objects or libraries into the foreign
command. The caller retains responsibility for an appropriate startup/library
sequence.

Strip flags take no value; joined forms such as `--strip-all=false` are
refused. A strip-looking argument consumed by `-u`, `--undefined` or another
value-taking option remains that option's value.

## What a dynamic PIE holds

The conversion (`foreign_link.c`) makes of the resolved inputs what GNU ld and
lld make of them, where the corpus test below compares it:

- **Libraries:** each needed library once, by its soname (or its path
  without one), in the resolver's order: g++'s driver names `-lgcc_s` twice,
  and the output has one `DT_NEEDED` for it. Version requirements are grouped
  by library, with the executable's own numbering.
- **Exports:** a definition that a needed library refers to, or a name that
  a library defines and the executable interposes, is exported once at its
  version, in canonical name order.
- **A library's own references** require nothing of the output: the loader
  binds them among the libraries, and a name that only libraries refer to gets
  no symbol, as lld leaves it under its default `--allow-shlib-undefined` for
  an executable whose libraries' dependencies it doesn't see. A library's
  reference doesn't make an `--as-needed` library needed either; only a
  regular object's or the command line's does.
- **Copy relocations,** as both linkers make them: a library's data that a
  retained fixup reaches other than through the GOT or by an address the
  loader relocates (GCC's `-fPIE` code reads `stdout` and `environ` by
  `PC32`) gets a copy in `.bss`, an `R_X86_64_COPY` relocation and an export,
  so that the library's own references bind to the copy. The library's other
  names at the same address (`environ`'s `_environ` and `__environ`), all of
  them, are defined at the copy and exported too, as lld's `getSymbolsAt`
  defines them; of those the link has no symbol of, a copy takes up to four,
  and refuses more.
  The copy is aligned by the largest power of two that divides the
  definition's address, at most its section's alignment, as lld aligns it,
  and is as large as the largest of its names. A function reached that way,
  which would need a canonical PLT entry, and a definition of no size are
  refused, naming the symbol. The imports are tested as a bitset, 16 fixups
  at a time on the vector path; only the hits, a few in a program, and the
  aliases (the library's export addresses compared eight at a time) are
  handled one at a time.
- **`__start_` and `__stop_`:** when something refers to `__start_NAME` or
  `__stop_NAME` and input sections are named `NAME`, a C identifier, those
  sections become one atom of their class, in input order, each at its
  alignment, with their symbols and fixups moved into it, and the two names
  are defined at its ends, global and protected, of no size, as GNU ld and
  lld define them. Each section is that atom to the passes after the
  conversion: GC roots a retained one's atom and keeps the FDEs of the code in
  it, and literal pooling leaves the atom's bytes as they are. Only an undefined name with one of the prefixes asks for
  this, so a link without one reads no section's name.
- **`STB_GNU_UNIQUE`** definitions keep their binding in `.symtab` and
  `.dynsym`.
- **The linker's names** (`_DYNAMIC`, `_GLOBAL_OFFSET_TABLE_`, the arrays',
  the ELF header's and the image's bounds) are defined with lld's
  visibilities: `end`, `_end`, `etext`, `_etext`, `edata`, `_edata` and
  `__bss_start` default, the others hidden. `_DYNAMIC` is defined whether or
  not anything refers to it.
- **IFUNCs:** a defined IFUNC that a fixup reaches gets a stub, as lld's
  `.iplt` entry, beside the imports' stubs: its calls go through it, and it
  stands for the IFUNC's address when anything else takes it (lld's canonical
  PLT entry). Its slot, after the GOT kinds', gets an `R_X86_64_IRELATIVE`
  relocation whose addend is the resolver, last in `.rela.dyn` (after the
  copies, as lld's `.rela.dyn` has its `.iplt` relocations last), and an
  IFUNC loaded through the GOT without a stub gets one for its own slot. The
  writer numbers the IFUNCs' stubs before the imports', so that a PIE without
  IFUNCs keeps its layout.

## What a static executable and a static PIE hold

The conversion gives the writer what `dynamic.c` gives the direct path's
static kinds (`dynamic_static_inputs` and `dynamic_static_pie_inputs`): the
start file's ABI note (`crt1.o`'s or `rcrt1.o`'s), and the link's
`__tls_get_addr`, which `libc.a` doesn't define and which stays undefined:
the writer relaxes each general- and local-dynamic access to local-exec,
consuming its call, as GNU ld and lld relax them in a static link. Every
other strong reference that nothing defines is still an error. The writer's
static runtime handling does the rest: IFUNCs' stubs in `.iplt` with
`IRELATIVE` relocations, which `libc.a`'s startup applies, in `.rela.iplt`
between `__rela_iplt_start` and `__rela_iplt_end` (lld and mold put them in
`.rela.dyn`), the prioritized init and fini array entries, and the names
`libc.a`'s startup looks for (the ELF header's and the image's bounds). A
static PIE also gets `DT_INIT` and `DT_FINI` from `crti.o`'s `_init` and
`_fini` and its `RELATIVE` relocations in `.rela.dyn` without RELR, as lld
writes `-static-pie` without `-z pack-relative-relocs`; `rcrt1.o`'s startup
applies them before anything else runs. A static program without libc takes
the same handling, which changes nothing for it, but for IFUNCs. The kernel
applies no relocations, so something in the program must apply their
`IRELATIVE` ones before it calls them, and the conversion takes them only from
a startup it recognizes (`foreign_link_startup_applies`): glibc's, by the files
its code comes from. The entry must be defined by the system's start file,
`crt1.o` for a static executable and `rcrt1.o` for a static PIE, the first
library directory's, where GCC's driver finds it (`runtime_library_identity`),
compared by identity with the file the resolution opened (device and inode,
from the open's `fstat`: a symbolic link to it counts, a copy elsewhere
doesn't), whose `_start` calls `__libc_start_main`. That must be defined by a
member of the system's `libc.a`, found the same way (one only in GCC's
installation isn't recognized, which can only refuse), whose `libc-start.o`
applies a static executable's relocations between `__rela_iplt_start` and
`__rela_iplt_end`,
which no input may define (the linker puts them around `.rela.iplt`). In a
static PIE it calls `_dl_relocate_static_pie`, which must come from `libc.a`
too (`dl-reloc-static-pie.o`) and applies the image's relocations through
`_DYNAMIC`, the linker's; glibc 2.44's also calls
`_dl_relocate_static_pie_ifunc` for the `IRELATIVE` ones, which must then come
from `libc.a` as well rather than be an empty one like `crt1.o`'s. Each step's
reference to the next name is checked too. Names alone prove nothing (#405's
reviews): a `_start` of its own that mentions `__libc_start_main` only in data
makes `libc.a`'s join, which refers on to everything above, but never runs it.
Another C library's startup isn't these files (musl's `__libc_start_main`
applies no `IRELATIVE` relocation), nor is a glibc installed elsewhere (a
sysroot's): a program whose startup applies them otherwise says so with
`--startup-applies-irelative`. The system's files are looked up (a few `stat`
calls) only for a static link with an IFUNC that a fixup reaches. Otherwise the
first IFUNC a fixup reaches is refused, naming it and why, where lld writes a
program whose call through the slot that nothing filled faults.

## What a shared library holds

A shared library's libraries and versions are a dynamic PIE's, without the
interpreter, the ABI note or copy relocations. Every global, weak and unique
definition of default or protected visibility is exported, as lld's `-shared`
exports them without a version script, and the writer makes the default ones
preemptible: their calls go through stubs reading GOT slots that the loader
fills, and their initial-exec, general- and local-dynamic TLS accesses get
their slots' relocations. A strong reference of default visibility to a name
nothing defines is left for the loader to bind (lld's `-z undefs`, its
default for `-shared`); one of another visibility is refused. `_init` and
`_fini` give `DT_INIT` and `DT_FINI`. A preemptible IFUNC binds as a dynamic
symbol, whose `GLOB_DAT` the loader binds by calling the resolver; one that
isn't preemptible (local, hidden or protected) needs an `IRELATIVE`
relocation, which the writer's shared mode doesn't write, and the first one a
fixup reaches is refused, naming it.

## Transformation and ownership contract

Conversion first resolves the selected objects and prunes discarded COMDAT
unwind records. Optional section GC uses the entry plus the adapter's normal
explicit, constructor/destructor, retained and dynamic-export roots. Its
deferred conversion mode lets GC discard unreachable strong references
before reporting remaining undefined symbols.

Optional CIE pooling follows GC and returns a terminal-executable link.
Optional literal pooling follows CIE pooling. Unsupported pooling inputs
leave the prior link intact; malformed input, memory and capacity failures
stop the command. Literal pooling does not authorize replay of original
relocations or unwind offsets. No such pass runs after it. The final writer
uses the selected lane count and exact/vector path.

The original `Foreign`, its input mappings, the names arena, every compact
link and the CIE history remain alive through writing. The command releases
them after the writer returns, including error paths. The terminal CIE
result is a caller contract, not a runtime type barrier; it must not go to
ET_REL output or another GC/original-import pass.

## Debug information

The importer leaves the inputs' non-allocated sections out of the link, so the
conversion builds the output's DWARF beside it (`foreign_debug.c`, #395): each
selected object's debug sections (`.debug_info`, `.debug_abbrev`,
`.debug_line`, `.debug_str`, `.debug_line_str`, `.debug_loclists`,
`.debug_rnglists`, `.debug_aranges`, `.debug_addr`, `.debug_str_offsets`,
`.debug_ranges`, `.debug_loc` and the rest of DWARF 4's and 5's), concatenated
by name in input order, each contribution at its alignment after the one
before, as GNU ld and lld concatenate them. `.debug_str`'s and
`.debug_line_str`'s strings aren't merged, which lld does: the sections are
larger, and only their offsets differ from lld's. A section of a COMDAT group
the resolution discarded goes with its group, and an excluded one
(`SHF_EXCLUDE`) is left out, as GNU ld and lld leave it out of a final link.
An empty one keeps its place, which a reference into it takes.

Each debug relocation is applied where the layout doesn't decide it and left
to the writer where it does:

- a reference into a debug section (`DW_FORM_sec_offset`'s and `DW_FORM_strp`'s
  `R_X86_64_32`, or `R_X86_64_64` in 64-bit DWARF) is that section's place in
  its output section plus the symbol's value and the addend, written now;
- a reference to code or data the link keeps (a `DW_AT_low_pc`'s or a line
  program's `R_X86_64_64`, an `R_X86_64_32` address, a thread-local's
  `R_X86_64_DTPOFF32` or `_64`) is the writer's, by the link's symbol
  (`ExecutableOptions.debug_sections`), which patches the definition's address
  or the thread-local's offset in the TLS segment after the layout;
- a reference to a section that COMDAT selection or `--gc-sections` dropped, or
  to an excluded debug section, or to a symbol the link has none of, gets lld's
  tombstone: 0, or 1 in `.debug_ranges` and `.debug_loc`, whose lists 0 would
  end.

Relocation types that DWARF doesn't use are refused, naming the object, the
section and the relocation, and so are a thread-local offset into a debug
section and a 4-byte field's value past its range: `R_X86_64_32`'s unsigned,
`R_X86_64_DTPOFF32`'s signed, as GNU ld and lld check them. A relocation
without a symbol is its addend, as GNU ld writes an address's (lld writes its
tombstone for any relocation without a symbol in a debug section). A compressed section (`SHF_COMPRESSED` of type `ELFCOMPRESS_ZLIB`, which
GNU as writes for `-g` by default and GCC's and clang's `-gz` ask for)
contributes its bytes decompressed by `inflate.c` (written from RFCs 1950 and
1951), at the size and alignment its compression header gives; another
compression (`-gz=zstd`'s) is refused, and so is a zlib stream that doesn't
decompress to its size or whose Adler-32 differs.

The build is a plan and two passes. The plan, on the calling thread or lane 0,
places each section in its output section and lists the contributions and the
relocation sections. The bytes pass copies or decompresses each contribution,
and the relocations pass applies each relocation section's relocations, each
into its own range of the records, which the end compacts in order. On lanes,
each pass's items go costliest first (a large object's sections are separate
streams to decompress) with a sync between the passes; the calling thread takes
them in order. Either way the same bytes and records come out. A relocation
section's relocations go eight at a time by the vector path: the plan resolves
each object's symbols once into a table of what they mean (a place in a debug
section, the link's symbol, a tombstone, or left to the exact path), so that a
batch takes one gather, then scatters its fields and compresses its records
for the writer. A batch with a rare relocation (a type DWARF doesn't use, a
field outside the section, a symbol through `SHN_XINDEX`, a value that doesn't
fit, fields out of order) goes to the exact path, its twin, which reads the
symbols themselves and which `--scalar` takes for all of them. A
DEFLATE stream is one chain of dependent codes, so its decoding stays serial,
spread over the lanes stream by stream; its Adler-32 is computed 64 bytes at a
time. `-S` and `-s` skip the whole build, and `--time` reports it as the
conversion's `debug` phase.

## Regular symbols

Regular-symbol selection follows all three optional transformations and uses
the final `Link` IDs, names, definitions and sizes, and the table is the one
lld writes for the same inputs, but for `STT_FILE` and unnamed `STT_SECTION`
bookkeeping, which aren't recovered:

- every named symbol but ID zero and the assembler's temporary labels, the
  locals named `.L...` that GNU as keeps only for relocations into
  `SHF_MERGE` sections (GCC's `.LC0` strings), which GNU ld and lld leave
  out;
- a hidden or internal global written as a local, as lld writes it;
- an undefined symbol of size 0, whatever an import's definition says;
- the linker's names with the visibilities above, and dead GC symbols absent
  rather than recreated as undefined rows; pooled literal symbols at their
  final shared addresses.

Locals, globals, weak definitions, imports, unresolved weak references, TLS,
absolute symbols, IFUNC resolvers and linker-defined image boundaries retain
the semantics of the [regular-symbol writer](executable-symbols.md).

`foreign_driver_symbols` runs in the existing writer lane group. Each vector
lane counts the selected names in a disjoint contiguous range using bounded
16-element loads; a named local of two bytes or more has the first two bytes
of its name tested for `.L`, eight names at a time, by gathers of the aligned
eight bytes that hold them, which never read outside a block the name has a
byte in. Lane zero prefixes those counts and makes one checked arena
allocation of exactly four bytes per selected ID. After publication, lanes
compact their ranges into disjoint output slices; another barrier makes the
increasing IDs visible to `executable_write`. The exact twin uses lane zero
and disables loop vectorization. Empty selection is valid and needs no ID
allocation. `-s` skips selection, its barriers and its allocation entirely.

The existing writer refuses a selected zero-sized TLS definition if its TLS
class has no output section, preserving an existing destination. Default
symbol output inherits that restriction; the same otherwise-supported input
can be written with `-s`.

## Validation

`test-foreign --cross-check` includes command parsing/defaults, ordered
archive and DSO search, entry extraction, failures, dynamic CRT execution,
exact/vector and 1/2/3/8-lane output identity, and GC and CIE option toggles
with retained unwind records. Named-symbol fixtures independently inspect
sizes, type, binding, visibility, final addresses, local ordering and imported
definitions, and the temporary labels' filter on every tail and split through
65 entries and at a `PROT_NONE` page. Both strip spellings must omit the
regular tables while leaving dynamic sections byte-identical. A combined
fixture checks all four CIE/literal pooling states, with and without GC,
across both paths and all four lane counts. `foreign_copy_test.c` links
assembly that reads a library of its own by `PC32`, as GCC's `-fPIE` code
reads a library's data (data with aliases, a weak alias, a 64-byte
alignment, read-only data and a version script, which the library reads back
through its GOT; and an object of eleven names, ten of them read through the
program's GOT, each of which the library reads back through its own), a C
program that reads libc's stdio streams and `environ`,
a program that needs the loader by a reference of its own, and the refusals
of a function and of an object of no size reached that way. Each, by both
paths and 1/2/3/8 lanes, writes a PIE that the loader runs, byte for byte
alike; with `--cross-check`, lld links the same inputs, and its copied names
(each one's version and size) and the names its COPY relocations copy must
be ours. `foreign_bounds_test.c` links a table in a section of one name from
two objects and an archive member that loads (and one that doesn't, whose
entry mustn't count) at alignments 8 and 16, a section writable in one object
and read-only in another, which merges into one writable section, a weak
reference to the bounds of a name no section has, which stays zero, an IFUNC
called and one that nothing refers to, each linked as a PIE that the loader
runs, and a shared library's hidden IFUNC called by its exported function
(refused, naming it); with `--cross-check`, lld's links of the same inputs
must run alike and define the bounds alike. The driver's own tests link a
shared library with `-soname` and with `-h`, check its `DT_SONAME`, export,
missing entry point and interpreter, and run a PIE that calls it; check that
`-pie` with `-static` writes a static PIE; refuse `-shared` with `-static`,
`--no-dynamic-linker` with `-pie` and `-soname` without a value; and link
programs without libc whose `_start` calls an IFUNC. They are refused, naming
the IFUNC and why, as a static executable and as a static PIE, with a pointer
to `__rela_iplt_start` or to `_DYNAMIC` that nothing reads, with a pointer to
`__libc_start_main` that nothing reads and a `__libc_start_main` of their own
that mentions what `libc.a`'s refers to (and a static PIE's relocator, which
mentions `_DYNAMIC`), and with a `_start` that applies the relocations itself
but without `--startup-applies-irelative`.
With it, a static executable whose `_start` applies those between
`__rela_iplt_start` and `__rela_iplt_end`, and a static PIE whose `_start`
applies `.rela.dyn`'s `RELATIVE` and `IRELATIVE` relocations through
`_DYNAMIC` from wherever the loader put it, link and run with their status;
the corpus links glibc's startup in both. With GCC's line around them in both
static modes (`foreign_driver_test_startup`: `crti.o`, crtbegin, libgcc and
`libc.a` in a group, crtend, `crtn.o`), `main` calling an IFUNC after the
system's start file links and runs, and so it does through a symbolic link to
the start file on 3 lanes, while a copy of the start file, a `_start` of its
own that mentions `__libc_start_main` only in data (on 3 lanes too) and the
system's start file with a `__libc_start_main` of the test's own are refused,
saying why. Its section GC case collects a link
after the merge: a
retained section whose bounds only dead code refers to must keep its bytes,
and a hook in a merged code section its FDE, while the dead code goes.

`test-foreign-corpus` is the differential test against lld and mold: AGENTS.md
("Foreign corpus test") has its rules. Each program of the corpus, compiled by
gcc and by clang at `-O2` as their default PIEs, is linked from one command
line by `link-foreign`, lld and mold as each kind of output: a dynamic PIE, a
static executable, a static PIE and a shared library of its library part
(compiled again with `-fPIC`), which a PIE of the rest loads; the three
outputs must run alike, have the same `.symtab` symbols with the same sizes,
and agree on the dynamic section, `.dynsym`'s versions and the dynamic
relocations, `IRELATIVE` ones among them, each by its resolver and whether
it writes a GOT slot or a pointer in data. What a reference
linker writes as its own valid choice is classified and counted, never
dropped silently: mold's `IRELATIVE` relocation of a pointer to an IFUNC
outside the GOT in a PIE, where `link-foreign` and lld relocate the pointer
relative to the IFUNC's stub, must be one of `link-foreign`'s stub pointers,
one for one. The foreign DWARF case (`foreign_debug_test.c`) compiles original C and
C++ fixtures with `-g` by gcc, whose GNU as compresses the debug sections, and by
clang, which doesn't, at `-O0` and `-O2`, in DWARF 5 and 4, and with
`-gz=zlib` and `-gz=none`, and links each as a PIE on 1 lane, on 8 and by the
exact path: the same bytes, which run and print what the program computes. The
C++ fixture's inline functions are COMDAT groups in both units, whose losing
copies' DWARF keeps tombstones; `--gc-sections` keeps the DWARF of what it
collects the same way; `-S` leaves the debug sections out. Assembly fixtures
check excluded debug sections (left out, with lld's tombstones in the
references into them), an empty section's place, and empty relocation
sections, also where no other section of the empty one's kind has bytes and the
link has no debug relocation, each as lld links them. A zstd section and a
damaged zlib stream are refused. The relocation passes' vector path is checked
against its exact twin on each debug relocation section of a `-g` object, which
ends at a `PROT_NONE` page, as compiled, cut to each length short of a batch,
and with each rare relocation at each of a batch's places. With
`--cross-check`, lld links the same objects: `llvm-dwarfdump --verify` finds no
error in either output, their `--statistics` agree but for the string sections'
sizes, and gdb, stopped in a function of each, gives the same backtrace,
arguments and locals but for addresses. `test-inflate` checks the decompressor
against gzip's streams of generated inputs at levels 1, 6 and 9, streams made by
hand bit by bit for each error, every cut and three changes of each byte of
streams to 4 KB ending at `PROT_NONE` pages, and the Adler-32's vector path
against its twin.
The corpus is its two original programs (C, with threads,
thread-locals, an archive, constructors, a weak reference, a registry and
IFUNCs called, loaded from the GOT and pointed to from data, and
C++, with iostream, exceptions across translation units, RTTI, templates in
COMDAT groups and a thread), and on request SQLite's shell, Lua's interpreter
and this repository's compiler, built by clang alone.

## Cost

`benchmark-link` times `link-foreign` beside GNU ld, lld, mold and wild on the
design's generated programs, from one command line, as a process of its own
with `--lanes=8` and `--lanes=1`. On the 9700X, one lane links the programs of
1 worker, 2,000 workers and synth's shape in 0.60, 1.12 and 55.2 ms, against
wild's 1.35, 2.56 and 99.9 ms and mold's 2.44, 3.31 and 99.9 ms on one thread:
0.89 GB/s of output per lane on the largest, against mold's 0.54; as a
static executable, a static PIE and a shared library, synth's shape takes
58.3, 59.3 and 65.6 ms on one lane, against mold's 100.1, 106.6 and 116.6 and
lld's 98.3, 102.5 and 128.2 on one thread. "Foreign executables, as built" in
the linker design has the tables, the appendix's own inputs and where the time
goes. GC, string pooling and CIE pooling remain
disabled by default; their own measurements are in the linker design.

With DWARF, SQLite's shell compiled by GCC at `-O2 -g` (GNU as's
zlib-compressed sections) links in 9.20 ms on one lane, 6.84 of them the
`debug` phase, mostly decompressing about 5 MB of DWARF, against lld's 29.3 ms
on one thread and 15.0 on eight and mold's 25.8 and 16.4; from uncompressed
sections (`-gz=none`), in 3.38 ms (0.96 of DWARF) against lld's 18.2 and 11.8
and mold's 14.7 and 13.1; with `-S`, in 1.76 ms. The relocations' vector path
takes the uncompressed DWARF in 0.96 ms against the exact path's 1.40
([the record](https://github.com/buster14a/buster-performance/tree/cabd9313cbdf76b566c0d63ed990c15925d466cf/docs/measurements/foreign-dwarf-395-2026-10-25)).
