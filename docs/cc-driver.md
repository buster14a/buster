# C driver build-system integration (#1113)

The C driver supports dependency output, buffered standard input, explicit static
and PIE linking, verbose diagnostics and the common compiler-information probes.
This is not a claim that every CFLAGS or LDFLAGS accepted by GCC/Clang works.

## Dependency options

`-M` / `-MM` preprocess without compiling and write Make rules to stdout, or to
`-MF FILE` / `-o FILE`. `-MD` / `-MMD` add dependency output to ordinary compilation,
linking, syntax checking or `-E`; without `-MF`, the output is the primary output
name with its suffix replaced by `.d`, or the source basename with that suffix.
An explicit primary output also supplies the default rule target. Otherwise the
source basename with `.o` is used, including for a default executable link.

`-MM` / `-MMD` exclude system-header observations and their system-context
children. `-MT TARGET` inserts a raw Make target, `-MQ TARGET` quotes a filename,
and repeated targets retain their command-line order. `-MP` adds empty rules for
selected headers, not the source, each on the next line with no blank line before it. Joined `-MFfile`, `-MTtarget` and `-MQtarget`
operands work too. `-MF -` explicitly sends dependencies to stdout.

```sh
./build/compiler -c -MMD -MP -MF build/unit.d -MT build/unit.o unit.c -o build/unit.o
```

The collector records active includes (including guard/once-skipped ones),
forced includes, successful `__has_include` probes and consumed `#embed` files,
not speculative cache opens. File identities deduplicate aliases, first
observation determines ordering/spelling, and a later non-system observation
clears an earlier system classification. The root source is always first and is
removed from the header records by identity. Diagnostic-history replay cannot
add records or write files. The serialization contract and its conventional Make
escaping limitations are in [driver-dependency-rules.md](driver-dependency-rules.md).

Multiple source files retain per-unit buffers until all units, and any requested
link, succeed. The command publishes in argv order, never from parallel workers.
An explicit shared `-MF` follows Clang's last-unit-wins behavior; stdout contains
all rules in order. `-M/-MM/-E` with several inputs and one `-o` is refused.

A named regular depfile is written to a private temporary sibling and renamed
only after a successful write and close. An old depfile survives compilation or
rule-construction failure. Each rename is atomic; a command publishing several
files is **not** a filesystem transaction, so a later publication failure can
leave earlier complete files replaced. Primary objects/executables are not
rolled back on a later dependency-output error. Existing input/output aliases,
including hard links, and the same new output spelled through an aliased parent
directory, are rejected before compilation. Dangling symlink aliases are
conservatively rejected. Publication replaces a regular-file symlink itself;
it does not promise to preserve that link. A new depfile gets the mode `fopen`
would give it (`0666` less the umask), and one replacing a regular file keeps
that file's permission bits, as clang's truncation does.
A symlink is not replaced: the bytes are written through it, so the target
changes and `lstat` still reports the link. `/dev/null` works as a sink and is
never replaced. Other special destinations are refused. Stdout writes are
checked but cannot be rolled back.

### Destination admission complexity (#1314)

`dependency_check_destinations_counted` observes each source, primary artifact
and dependency destination once. For multiple implicit destinations, its first
scan produces a compact array of protected source/artifact identities, skipping
stdin. `dependency_index_prepare` builds three independent sorted pointer
columns over that immutable array: full path, existing `(device, inode)`, and
existing parent `(device, inode, basename)`. A path contributes every applicable
key; an existing-file key does not replace its other keys. Later destinations
use `dependency_index_overlaps` to search the columns. A dangling symlink on
either side still rejects an overlap conservatively when the opposite set is
nonempty. Repeated sources, artifacts and dependency destinations are allowed;
only a dependency destination overlapping a source or primary artifact fails.

The index owns no strings or filesystem resources. It borrows the command's
NUL-terminated path spellings and its arena-resident identity records until
admission ends. Each column has capacity equal to the protected-path count,
but lookup and sorting read only its initialized entries. In-place iterative
heapsort and binary search bound key comparisons by `O(N log N)` for `N`
units, including duplicate or adversarial key order. String comparisons still
cost the bytes examined. Filename and index storage and filesystem probes
remain `O(N)`; implicit objects allocate one name each. A single/shared
destination uses a direct scan without an index. Stdout, disabled and empty
requests retain their no-work paths, and exhausted capacity refuses admission
before any output is opened.

The driver tests exercise index queries against the independent filesystem
alias matrix and synthetic keys, including empty sets, duplicates, invalid
identity fields, dangling links and capacity failures. The admission regression
counts actual filesystem probes and key comparisons through 1,024 units and
bounds arena use and growth on successive doublings. These are deterministic
complexity checks, not timing measurements. The index currently uses scalar
ordering and lookup; SIMD sorting and end-to-end `-MMD -c` throughput remain
9700X measurement questions. Admission retains its existing nontransactional
snapshot semantics against concurrent external filesystem changes.

## Standard input and information probes

`-E -`, `-E -dM -` and `-x c -` read standard input once into bounded command-owned
memory. The source is named `<stdin>` for diagnostics, and replay reads the same
bytes. It contributes no filesystem dependency; included headers still do.
Compilation requires an explicit language for stdin. Repeated `-` inputs are
rejected rather than racing or reading an exhausted stream.

A primary input that is a character device, FIFO, socket, or `/dev/fd/N` pipe
is read the same way, once, with `read()` until EOF. `/dev/null` is an empty
translation unit, so `-dM -E -x c /dev/null` prints the predefines. The bytes
are cached on the command for a replay and for `--id-order`'s second pass.
Include search does not change: a speculative probe of a FIFO stays
non-blocking and still is not a regular file.

```sh
printf 'int value;\n' | ./build/compiler -x c -c -MMD - -o build/stdin.o
```

`-v` writes identification, target and integrated-stage information to stderr,
without corrupting preprocessed text or dependency rules on stdout. A bare `-v`
is a successful information probe. `--version` and `-dumpmachine` remain supported.
`-dumpversion` and `-dumpfullversion` currently report **0.0.0**, the unversioned
Buster development driver, not a spoofed Clang release.

`-print-file-name=NAME` searches explicit `-L` directories and the actual runtime
library paths; unknown names are returned unchanged. An empty `-print-file-name=`
prints a blank line, not a directory. `-print-prog-name=NAME`
searches PATH for an executable, without claiming the integrated compiler invokes
that program. `-print-search-dirs` reports the executable installation directory,
PATH and runtime search directories. Giving both print options prints the
directories and then the file path. These probes do not import runtime snapshots.
Option operands that happen to spell actions (`-MF -E`, `-MT --version`) are not
reinterpreted as driver actions. `--` ends option recognition.

## Link kinds and remaining limits

`-pie` selects the normal dynamic PIE. `-static` and `-static-pie` select the
existing static and static-PIE runtime, binding and executable-writing paths.
Static PIE takes precedence over static, and either static option over `-pie`,
regardless of argument order, as Clang's driver does. Static library search never
falls back to a `.so`.

The existing direct linker still rejects nonempty user `-l` archives. This change
does not implement general archive extraction beyond the runtime's own archives.
`-fPIC` / `-shared` still require real preemption and shared-library TLS support
(#811). Stack-protector and control-flow-protection requests remain explicit
refusals, not ignored security flags. `-S`, `-save-temps`, general object-input
linking and macro-only forced includes (`-imacros`) are not added here. These
limits mean **#1113 remains open**; dependency support alone does not make Buster
a universal drop-in CC for CMake/autotools projects.

## Regression coverage and provenance

`c_driver_compatibility_test.c` is called by `test-checker-driver`. Its original
fixtures run the real compiler executable, compare short dependency rules with
Clang (ignoring only empty physical lines around phony rules), exercise stdin,
symlinked depfiles, non-regular sources, combined `-print-*` probes,
parallel-unit publication, aliases and failure preservation, consume escaping
through GNU Make, and execute all three link kinds with TLS and libc. ELF type
and interpreter headers independently verify the link mode. It can also be built
as a standalone harness, with the compiler path as its first operand:

```sh
clang -std=c17 -O1 -Wall -Wextra -Werror -DBUSTER_DRIVER_COMPAT_STANDALONE src/buster/compiler/c_driver_compatibility_test.c -o /tmp/buster-driver-compat && /tmp/buster-driver-compat "$PWD/build/compiler"
```

The standalone serializer has separate scalar/native, generated-filename and
guard-page tests. Local smoke builds are not substitutes for the official
multi-translation-unit Clang 23 build, complete correctness CI or 9700X native
qualification. No whole-driver throughput improvement is claimed.

Implementation and fixtures are original project material under Apache-2.0 WITH
LLVM-exception. Installed Clang and GCC are external behavioral oracles and GNU
Make is an external consumer; no upstream implementation, fixture or generated
metadata is incorporated. GCC's documented
[preprocessor options](https://gcc.gnu.org/onlinedocs/gcc/Preprocessor-Options.html)
and [driver options](https://gcc.gnu.org/onlinedocs/gcc/Overall-Options.html)
provide interface facts only.
