# Raw foreign linker command

`build/compiler link-foreign` connects the foreign input resolver, conversion
and optional transformations to the built-in executable writer. It accepts
raw linker inputs; callers supply startup objects and libraries in link
order. It does not invoke a system linker to produce its output.

```text
build/compiler link-foreign [-o OUTPUT] [-e ENTRY] [-static|-pie]
    [--dynamic-linker PATH] [--lanes N] [--scalar]
    [--gc-sections|--no-gc-sections]
    [--merge-strings|--no-merge-strings]
    [--dedup-unwind|--no-dedup-unwind]
    INPUT... [-L DIR] [-l LIB] [-u NAME]
```

Defaults are a static executable named `a.out`, entry `_start`, one lane,
and all optional transformations disabled. `--scalar` selects the exact
resolver and writer paths. `--help` describes the supported command forms.
The entry is a forced undefined reference during resolution, so an archive
can supply it; final entry lookup is repeated after symbol remapping.

## Input order and output boundary

Driver options are removed without reordering paths, `-L`, `-l`, `-u`,
`--undefined`, archive groups, whole-archive, as-needed or library-search
controls. The final static mode starts library search with archives only;
PIE starts with the resolver's ordinary DSO preference. Explicit ordered
`-Bstatic`/`-Bdynamic` override that initial choice. An explicit `-static`
also stays in the input stream, preserving its existing search effect.
Static output that selects a needed DSO reports the incompatible input.

The initial static command supports no-libc startup. Dynamic PIE accepts
explicit C runtime startup objects, the program objects, libraries and
closing CRT objects in the caller's chosen order. `--dynamic-linker PATH`
supplies the interpreter directly; otherwise the command discovers the
system interpreter through the existing runtime helper. Neither form inserts
startup objects or libraries into the foreign command. The caller retains
responsibility for an appropriate startup/library sequence.

Shared-library output, static libc startup and static PIE are separate
follow-ups. The command does not accept compiler-driver options such as
`-Wl,...`, or silently ignore unsupported linker flags. The current writer
path does not attach a regular `.symtab`/`.strtab`; symbol-table equivalence
with another linker is consequently not claimed. A later named-symbol
producer must consume the final remapped symbol identities explicitly.

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

## Validation and cost

`test-foreign --cross-check` includes command parsing/defaults, ordered
archive and DSO search, entry extraction, failures, dynamic CRT execution,
exact/vector and 1/2/3/8-lane output identity, and GC and CIE option toggles
with retained unwind records. A combined fixture checks all four CIE/literal
pooling states, with and without GC, across both paths and all four lane
counts. Original duplicate literals use supported ABS64 references so both
pooling passes must produce their independently checked savings. The
adapter's C++ throw/catch/destructor gate
is a separate test, not a command-line C++ fixture. Output execution uses
direct spawning and a private environment that replaces `LD_LIBRARY_PATH`
without changing the parent environment; paths containing `=` are covered.
The general test helper's ordinary environment behavior stays unchanged.

The earlier composed driver snapshot passed 488 driver checks and 55 CLI
CIE checks on the shared 9700X. That receipt is not validation of this newer
composition of main, accepted string pooling, GC and CIE pooling. Its own
build and full cross-check remain required, as do the interpreter-discovery
follow-up's independent correctness and cost gates.

The optional passes retain their documented measurement limits. Their
synthetic phase or whole-link fixtures do not establish corpus performance
for this command. Default interpreter discovery currently performs more
work than path lookup alone; its isolated replacement is being validated.
GC, string pooling and CIE pooling remain disabled by default. Issue #86
remains open for the remaining output, symbol and corpus requirements.
