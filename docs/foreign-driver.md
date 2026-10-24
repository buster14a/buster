# Raw foreign linker command

`build/compiler link-foreign` links other compilers' objects: it connects the
foreign input resolver, conversion and optional transformations to the
built-in executable writer. It takes raw linker inputs, as GCC's and clang's
drivers hand them to GNU ld; callers supply startup objects and libraries in
link order. It does not invoke a system linker to produce its output.

```text
build/compiler link-foreign [-o OUTPUT] [-e ENTRY] [-static|-pie]
    [--dynamic-linker PATH] [--lanes N] [--scalar] [-s|--strip-all] [--time]
    [--gc-sections|--no-gc-sections]
    [--merge-strings|--no-merge-strings]
    [--dedup-unwind|--no-dedup-unwind]
    INPUT... [-L DIR] [-l LIB] [-u NAME]
```

Defaults are a static executable named `a.out`, entry `_start`, one lane,
and all optional transformations disabled. Named regular symbols are emitted
by default; `-s` and `--strip-all` omit `.symtab` and `.strtab` while preserving
the dynamic symbol table and runtime dependencies. `--scalar` selects the exact
resolver, symbol-selection and writer paths. `--time` reports, on standard
error after the link, each stage's wall time (the arguments, the interpreter's
discovery, the resolution with each of its phases on the slowest lane, the
conversion, the optional passes, the write with each of the writer's phases on
lane 0, and the release), as wild's `--time` and mold's `--perf` report
theirs. `--help` describes the supported
command forms. The entry is a forced undefined reference during resolution,
so an archive can supply it; final entry lookup is repeated after symbol
remapping.

The options GCC's driver gives GNU ld that describe what every output of this
command is already are taken and change nothing: `--build-id`,
`--eh-frame-hdr`, `--hash-style=gnu`, `-m elf_x86_64` (and `-melf_x86_64`),
and `-z now`, `-z relro` and `-z noexecstack`. `-dynamic-linker` is
`--dynamic-linker`. Any other `-m` or `-z` keyword is refused, naming it. So
a driver's command line, as `gcc -###` prints it for GNU ld, links as it is,
with `-pie` for a PIE.

## Input order and output boundary

Driver options are removed without reordering paths, `-L`, `-l`, `-u`,
`--undefined`, archive groups, whole-archive, as-needed or library-search
controls. The final static mode starts library search with archives only;
PIE starts with the resolver's ordinary DSO preference. Explicit ordered
`-Bstatic`/`-Bdynamic` override that initial choice. An explicit `-static`
also stays in the input stream, preserving its existing search effect.
Static output that selects a needed DSO reports the incompatible input.

The static output supports no-libc startup. Dynamic PIE accepts explicit C
runtime startup objects, the program objects, libraries and closing CRT
objects in the caller's chosen order. `--dynamic-linker PATH` supplies the
interpreter directly; otherwise the command finds the system interpreter as
[the interpreter note](foreign-interpreter.md) describes. Neither form inserts
startup objects or libraries into the foreign command. The caller retains
responsibility for an appropriate startup/library sequence.

Shared-library output, static libc startup and static PIE are not supported
yet. Strip flags take no value; joined forms such as `--strip-all=false` are
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
  names at the same address (`environ`'s `_environ` and `__environ`) are
  defined at the copy and exported too, as lld's `getSymbolsAt` defines them.
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
  lld define them. Only an undefined name with one of the prefixes asks for
  this, so a link without one reads no section's name.
- **`STB_GNU_UNIQUE`** definitions keep their binding in `.symtab` and
  `.dynsym`.
- **The linker's names** (`_DYNAMIC`, `_GLOBAL_OFFSET_TABLE_`, the arrays',
  the ELF header's and the image's bounds) are defined with lld's
  visibilities: `end`, `_end`, `etext`, `_etext`, `edata`, `_edata` and
  `__bss_start` default, the others hidden. `_DYNAMIC` is defined whether or
  not anything refers to it.
- **IFUNCs:** a defined IFUNC that a fixup reaches is refused, by its name
  and object, rather than written as a plain function, which would call the
  resolver: the writer's IFUNC stubs and IRELATIVE relocations are the static
  runtime's, which a dynamic PIE's stubs for its imports don't share yet. One
  that nothing reaches is written as it is.

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
through its GOT), a C program that reads libc's stdio streams and `environ`,
a program that needs the loader by a reference of its own, and the refusals
of a function and of an object of no size reached that way. Each, by both
paths and 1/2/3/8 lanes, writes a PIE that the loader runs, byte for byte
alike; with `--cross-check`, lld links the same inputs, and its copied names
(each one's version and size) and the names its COPY relocations copy must
be ours. `foreign_bounds_test.c` links a table in a section of one name from
two objects and an archive member that loads (and one that doesn't, whose
entry mustn't count) at alignments 8 and 16, a section writable in one object
and read-only in another, which merges into one writable section, a weak
reference to the bounds of a name no section has, which stays zero, and an
IFUNC called (refused, naming it) and one that nothing refers to (linked);
with `--cross-check`, lld's link of the same inputs must run alike and define
the bounds alike.

`test-foreign-corpus` is the differential test against lld and mold: AGENTS.md
("Foreign corpus test") has its rules. Each program of the corpus, compiled by
gcc and by clang at `-O2` as their default PIEs, is linked from one command
line by `link-foreign`, lld and mold; the three executables must run alike,
have the same `.symtab` symbols with the same sizes, and agree on the dynamic
section, `.dynsym`'s versions and the dynamic relocations. What a reference
linker writes as its own valid choice is classified and counted, never
dropped silently. The corpus is its two original programs (C, with threads,
thread-locals, an archive, constructors, a weak reference and a registry, and
C++, with iostream, exceptions across translation units, RTTI, templates in
COMDAT groups and a thread), and on request SQLite's shell, Lua's interpreter
and this repository's compiler, built by clang alone.

## Cost

`benchmark-link` times `link-foreign` beside GNU ld, lld, mold and wild on the
design's generated programs, from one command line, as a process of its own
with `--lanes=8` and `--lanes=1` ("The whole link" in the linker design, where
the 9700X's numbers are). GC, string pooling and CIE pooling remain disabled
by default; their own measurements are in the linker design.
