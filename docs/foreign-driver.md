# Raw foreign linker command

`build/compiler link-foreign` connects the foreign input resolver, conversion
and optional transformations to the built-in executable writer. It accepts
raw linker inputs; callers supply startup objects and libraries in link
order. It does not invoke a system linker to produce its output.

```text
build/compiler link-foreign [-o OUTPUT] [-e ENTRY] [-static|-pie]
    [--dynamic-linker PATH] [--lanes N] [--scalar] [-s|--strip-all]
    [--gc-sections|--no-gc-sections]
    [--merge-strings|--no-merge-strings]
    [--dedup-unwind|--no-dedup-unwind]
    INPUT... [-L DIR] [-l LIB] [-u NAME]
```

Defaults are a static executable named `a.out`, entry `_start`, one lane,
and all optional transformations disabled. Named regular symbols are emitted
by default; `-s` and `--strip-all` omit `.symtab` and `.strtab` while preserving
the dynamic symbol table and runtime dependencies. `--scalar` selects the exact
resolver, symbol-selection and writer paths. `--help` describes the supported
command forms.
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
`-Wl,...`, or silently ignore unsupported linker flags. Strip flags take no
value; joined forms such as `--strip-all=false` are refused. A strip-looking
argument consumed by `-u`, `--undefined` or another value-taking option remains
that option's value.

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

Regular-symbol selection follows all three optional transformations and uses
the final `Link` IDs, names, definitions and sizes. It excludes ID zero and
unnamed rows. Locals, globals, weak definitions, imports, unresolved weak
references, TLS, absolute symbols, IFUNC resolvers and linker-defined image
boundaries retain the semantics of the
[regular-symbol writer](executable-symbols.md). Dead GC symbols are absent
rather than recreated as undefined rows; pooled literal symbols use their
final shared addresses. Original `STT_FILE` and unnamed `STT_SECTION`
bookkeeping are not recovered. This is a named semantic table, not complete
symbol-table equivalence with another linker.

`foreign_driver_symbols` runs in the existing writer lane group. Each vector
lane counts nonzero names in a disjoint contiguous range using bounded
16-element loads. Lane zero prefixes those counts and makes one checked
arena allocation of exactly four bytes per selected ID. After publication,
lanes compact their ranges into disjoint output slices; another barrier makes
the increasing IDs visible to `executable_write`. There is no padded-tail
contract or name-byte copy. The exact twin uses lane zero and disables loop
vectorization. Empty selection is valid and needs no ID allocation. `-s`
skips selection, its barriers and its allocation entirely. The descriptor,
selection and final link live through the writer; command arena teardown
releases them. Selection, synchronization and cleanup belong to the whole
command's cost.

The existing writer refuses a selected zero-sized TLS definition if its TLS
class has no output section, preserving an existing destination. Default
symbol output inherits that restriction; the same otherwise-supported input
can be written with `-s`. This is a compatibility limitation requiring a
separate writer decision, not a claim that such ELF inputs are invalid.

The original `Foreign`, its input mappings, the names arena, every compact
link and the CIE history remain alive through writing. The command releases
them after the writer returns, including error paths. The terminal CIE
result is a caller contract, not a runtime type barrier; it must not go to
ET_REL output or another GC/original-import pass.

## Validation and cost

`test-foreign --cross-check` includes command parsing/defaults, ordered
archive and DSO search, entry extraction, failures, dynamic CRT execution,
exact/vector and 1/2/3/8-lane output identity, and GC and CIE option toggles
with retained unwind records. Named-symbol fixtures independently inspect
sizes, type, binding, visibility, final addresses, local ordering and imported
definitions. Both strip spellings must omit the regular tables while leaving
dynamic sections byte-identical. Guarded selection inputs/outputs cover every
tail and split through 65 entries; exhausted-arena tests require all lanes to
converge before writing. Existing failure sentinels cover invalid strip flags,
unresolved input and missing TLS sections. A combined fixture checks all four
CIE/literal pooling states, with and without GC, across both paths and all four lane
counts. Original duplicate literals use supported ABS64 references so both
pooling passes must produce their independently checked savings. The
regular table must agree with those final relocated literal addresses and
omit GC and COMDAT losers. The adapter's C++ throw/catch/destructor gate
is a separate test, not a command-line C++ fixture. Output execution uses
direct spawning and a private environment that replaces `LD_LIBRARY_PATH`
without changing the parent environment; paths containing `=` are covered.
The general test helper's ordinary environment behavior stays unchanged.

The earlier composed driver snapshot passed 488 driver checks and 55 CLI
CIE checks on the shared 9700X. That receipt is not validation of this newer
composition of main, accepted string pooling, GC and CIE pooling. Its own
build and full cross-check remain required, as do the interpreter-discovery
follow-up's independent correctness and cost gates.

The new default-symbol adapter and fixtures are currently source-reviewed
only. Their locked target-host build/runtime gates, assembly inspection and
whole-command selection-plus-writer measurements remain pending. Existing
writer attachment evidence does not qualify this producer or imply that
default regular symbols are free.

The optional passes retain their documented measurement limits. Their
synthetic phase or whole-link fixtures do not establish corpus performance
for this command. Default interpreter discovery currently performs more
work than path lookup alone; its isolated replacement is being validated.
GC, string pooling and CIE pooling remain disabled by default. Issue #86
remains open for the remaining output, symbol and corpus requirements.
