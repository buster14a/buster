# Many translation units in one process

[#152](https://github.com/buster14a/buster/issues/152): the C driver compiles
every source file of its command line in one process.

```sh
./build/compiler [options] a.c b.c ... -o prog    # one executable of them all
./build/compiler -c [options] a.c b.c ...         # a.o, b.o, ...
./build/compiler -fsyntax-only [options] a.c b.c ...
```

With one source file the driver runs as before, byte for byte. With several:

- **`-fsyntax-only`** checks each unit.
- **`-c`** writes each unit's object, named as clang names it: the source's
  base name with `.o`, in the current directory. `-o` with `-c` is refused, as
  clang refuses it ("cannot specify -o when generating multiple output
  files"), and so are two units whose objects would have one name, which clang
  would write twice.
- **The default command** links one executable of them all (`-o`, `a.out` by
  default) through the direct path (#157): the units' links, merged into one
  and bound against the C runtime's snapshot as one unit's link is
  (`check_driver_executable`). `-l` and `-L` work as for one unit.
- `--dump-typed` and `--dump-static` take one source file, and `-g` for an
  executable of several units is refused for now (`-c -g` writes each unit's
  object with its DWARF).

Every unit is compiled whatever the others do, as clang compiles them: a unit
with errors writes no object, and the others write theirs; any unit's error
makes the exit status 1 and leaves no executable. The diagnostics come in the
command line's order, each unit's exactly as it alone would print them.

`src/buster/compiler/c_checker_units.c` is the implementation, and
`c_checker_driver.c`'s `check_driver_unit` the pipeline each unit runs, the one
a command of one file runs.

## The units on lanes

The units are a lane group's items (`lane.c`), taken costliest first from one
counter (`lane_take`), their costs their source files' sizes: the largest
first, so that the last ones to finish are small, and the command line's
order among equals. Each unit runs on one lane from preprocessing to its code:
the parse, the checker, the selection, the lowering, Tier 0, branch sizing,
encoding and the object's write or the link's copy. A unit's functions compile
on its lane alone. #151's function lanes inside a unit's lane would be a lane
group inside another, which `lane_run` refuses, and with several units the
units fill the lanes: on the 9700X's eight cores, more than a few units keep
every lane busy.

The driver runs at most one lane per unit, and by default one per CPU the
process may run on; `--lanes N` runs N, and the worker budget (#673:
`--worker-budget`, GNU make's jobserver) limits the lanes past the calling
thread's own as it limits #151's. Lanes that can't start leave their units to
the calling thread, which compiles them alone: the same outputs.

Before the lanes start, the calling thread builds the tables every unit's code
reads and the first unit to need them would otherwise build: the parser's
(through `parse_initialize`, in the front end's home: `main.c`), the
encoder's, the semantics' and Tier 0's, and with unwind tables, theirs. The
lanes then only read them.

## What the units share

Each lane keeps an intern table and a file cache (#150's `FileCache`) that the
units it compiles preprocess with, one after another: a header that the lane's
units include is read, lexed, refined and given its directive table once on
the lane, and a path once probed. The lanes share nothing that a unit writes,
so no lane waits for another, and no structure is shared between threads but
the tables built before the lanes.

A unit's own state is its own, as #152's design asks: its macro table, its
bindings and its type table (tags and their types, and the builtin types each
table makes for itself). Its identifiers that the lane's earlier units gave
ids keep them, and its other identifiers get the next ones: the ids a unit
sees depend on which units its lane compiled before it. Ids are opaque
([docs/determinism.md](determinism.md)): nothing a unit writes depends on
them, so a unit's object is the same bytes whichever lane compiles it and
after whichever units. `test-check-units` checks it: each unit's object of a
command of several units is its object alone, at every lane count.

### Not shared across lanes, yet

#152's design shares one file cache and one intern table between all the
units, so that a header is prepared once in the process, as #150's run-ahead
walk prepares a command's files on lanes. A unit's preprocessing writes both:
the spellings that token pasting makes, the paths its `__has_include` and
computed includes probe, and a file the walk didn't prepare (pp_run's
`intern_find_or_add`, `file_cache_lookup_path` and `file_cache_prepare`). One
cache for units running at once on several lanes needs those writes made
safe for readers on other lanes: a lock for the writers and tables that never
move while lanes read them, or each lane's writes into an overlay of its own.
Each lane's cache prepares a header at most once per lane instead; what that
costs against preparing it once is measured below.

### Diagnostics

Each unit writes its diagnostics, the preprocessor's (`PPOptions.diagnostics`)
and the driver's own, to a stream of its own (`open_memstream`). Once every
unit has run, the calling thread prints them in the command line's order.

## The link

A unit that links keeps its link (`check_units_keep`): its code and data as
`-c` would write them (its emission without binding, its static data
published, `link_check` passed), copied into its lane's arenas with every
record's id as it was, and which of its undefined global symbols a system
header declared, for the error that names their library. Its other memory goes
back to the lane for the next unit.

Once every unit has run, `check_units_merge` joins the units' links into one,
in the command line's order, as the emission builds one unit's
(`check_program_emit`):

1. **Symbols.** Each unit's symbols in its order. A global name is one symbol
   for all the units, found in a table of the names' hashes; a local symbol is
   its unit's own. A name that two units define is an error ("multiple
   definition of 'x'"); a weak definition yields to a strong one. The symbol
   takes its definition's flags, the most constraining visibility any unit
   gives it, and for a reference a weak binding only when every unit's
   reference is weak. The units' definitions are the program's
   (`BUSTER_LINK_SOURCE_PROGRAM`).
2. **The runtime.** `bind_begin` joins the C runtime between the program's
   symbols and its atoms, its units numbered after the program's.
3. **Atoms.** Each unit's atoms in its order, in a unit of the link of their
   own (so that `link_check`'s rule that a local symbol is referred to only from
   its own unit holds), each with its fixups, their targets the merged
   symbols; then its priorities and its definitions.

The link's room for the runtime's records is `bind_capacity_needed`'s for the
names no unit defines. Then `check_driver_executable` binds and writes the
executable as it does one unit's: `bind_end`, `link_check`, `dynamic_inputs`
and `executable_write`, with the same errors, and once more from a fresh
runtime when the snapshot is damaged.

A unit's code reaches the symbols other units define as it reaches any it
doesn't define: through the GOT, which the executable's write relaxes for
the image's symbols (`call` to `addr32 call`, `mov` to `lea`). Binding every
external name before instruction selection, which a link of several units
makes possible (`docs/object-writer-and-linker.md`, "Binding before
instruction selection"), is a later step.

### The symbol table

The direct path's executables now carry the program's symbols in `.symtab`
(`ExecutableOptions.symbols`), as a linker writes its objects': every named
symbol of the program's but the compiler's `.L` labels, which an assembler
leaves out of its objects, and a TLS variable of no size, which the writer
has no section for. With `-g`, the DWARF unit's function symbols stay what
they were. So `nm` and `perf` name an executable's functions, and
`test-check-units` compares the symbols an executable of several units
defines with those of the units' objects linked by `cc`.

## Tests

`./build/compiler test-check-units [--executable PATH] [--clang PATH]
[--seed N] [--programs N] [--keep] [--corpus DIR [--option ARG]...]` runs the
driver as a process, in a directory of its own:

- **Programs:** generated programs of 2 to 7 units, whose functions call
  other units', read and write their globals (initialized, zero-filled and
  thread-local), take their functions' and globals' addresses into tables and
  pointers, pass structs by value, call `printf`, and have statics and static
  functions of the same names as the other units'. Each program's executable
  of all its units, by `--lanes` 1, 2, 3 and 8 and by default and by each
  path, is the same bytes, runs as `clang -O0`'s build of the program does,
  and defines the symbols (by name, binding and kind) that `cc`'s link of the
  units' objects compiled one at a time defines; that link runs as clang's
  too. Each unit's object of `-c` of all the units is its object alone, at
  every lane count.
- **Diagnostics:** a unit with a warning, one with an error and a clean one,
  with `-c`, `-fsyntax-only` and into an executable, at every lane count:
  standard error is each unit's alone, in the command line's order; `-c`
  writes the objects of the units without errors only, and the link writes
  nothing.
- **Refusals:** a name two units define, a name none defines, `-c -o`, a dump,
  `-g` for an executable, two units of one object's name.
- **Lanes that don't start**, in a child limited to the processes it has:
  one lane's outputs.
- **`--corpus DIR`:** every `.c` file in DIR at once with `-c`, by 8 lanes,
  against each file alone: the same statuses, diagnostics and objects.
