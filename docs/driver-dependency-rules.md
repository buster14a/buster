# Dependency-rule serialization (part of #805)

## Status

`src/buster/compiler/c_dependency.c` provides the bounded Make-rule serializer.
The C driver now calls it through `c_dependency_driver.c` and the preprocessor's
consumed-file observations (#1113), for `-M`, `-MM`, `-MD`, `-MMD`, `-MF`, `-MT`,
`-MQ` and `-MP`. [The CC driver guide](cc-driver.md) specifies output naming,
standard input, parallel units, publication and the remaining compatibility gaps.

The standalone C tests live in `tests/driver/dependency_rule.c`. They are
registered in `tools/ci/correctness.tests`, which builds scalar and native variants
and runs both with `--oracle`. End-to-end cases in `c_driver_compatibility_test.c`
are called by `test-checker-driver`; they invoke the real compiler executable.

## Input ownership and collection contract

The caller supplies the project's bounded `String8` type before including the
module. `dependency_rule` borrows a `DependencyRule` and its strings and arrays.
The caller owns both the output allocation and the dependency collection.

`source` is the first prerequisite unless `omit_source` denotes standard input,
which has no filesystem prerequisite. `object` is the already-resolved
default target; the serializer does not derive object filenames. With explicit
`targets`, their command-line order is retained and `object` is not read. An
unquoted target is a verbatim Make expression, as with `-MT`; a quoted target
requests the filename escaping used for `-MQ`. The default target and all
prerequisite/phony names are quoted automatically.

The `files` array must contain one record per file identity, in first-use order.
It is **not** an arbitrary dump of the file cache. `used` means the translation
unit actually consumed that file, not that run-ahead happened to open or tokenize
it. `system` must mean every consumption was in a system-header context; a later
non-system consumption clears it. This includes the context inherited by nested
includes, not merely whether an include spells angle brackets. Deduplication and
that contextual provenance belong to the collector, not the serializer. The
serializer skips a record spelling the root source, so it is not repeated and
never receives a phony rule.

`include_system` selects all consumed headers or only non-system headers. `phony`
adds an empty rule for each selected header, but never for the main source.
Unused and filtered system records are not dereferenced through their path.
The fixtures exercise these contracts using supplied records; they do not prove
that the existing preprocessor produces them.

## Representation and failure behavior

The format follows GCC/Clang dependency quoting: dollars are doubled, hashes and
spaces/tabs are escaped, and backslashes immediately before escaped whitespace
are doubled. Runs spanning a 64-byte classification boundary retain their state.
Raw `-MT` expressions otherwise retain their bytes. Names containing NUL, CR, or
LF are rejected rather than emitting truncated names or additional rule lines.
An empty explicit target is representable; any command-line policy for empty
option operands belongs to the driver. Source, default target, and selected
header names must be nonempty.

This is the conventional compiler dependency escaping, not a general encoder for
all possible GNU Make syntax or filenames. In particular, other Make punctuation
is not transformed into a different syntax. Long rules are emitted on one line;
there is no GCC/Clang column-wrapping policy to reproduce byte for byte.

A null output with zero capacity queries the required byte count. A real write
preflights the complete input and required size before touching the output.
`DEPENDENCY_CAPACITY` reports the required size and preserves the destination;
malformed input and count overflow report zero bytes and also preserve it. The
result is an exact byte slice without an appended NUL. Output, the `written`
result, and all input records/strings must be disjoint and valid for their stated
lengths; inputs remain unchanged throughout both passes. The module does not
open files, allocate memory, or itself provide atomic filesystem publication.

Classification has a bounded AVX-512BW implementation and a scalar twin selected
by `rule.scalar`. Masked tail loads read no byte beyond the slice. A build without
AVX-512F/BW compiles only the scalar path. Repeated immutable descriptor fields
are cached in locals; no `restrict` assumptions or strict-aliasing changes are
introduced. No throughput improvement is claimed without a representative 9700X
measurement after integration.

## Standalone validation

Run from the repository root. The optional `--oracle` cases require `gcc` and
`clang` in `PATH`; absent tools fail rather than being counted as success. They
compare short raw/quoted target rules against each compiler, using original C
fixtures in a private temporary directory. The default tests need no external
compiler execution.

```sh
clang -std=c17 -O2 -march=x86-64 -fwrapv -funsigned-char -fno-strict-aliasing -Wall -Wextra -Werror tests/driver/dependency_rule.c -o /tmp/buster-dependency-scalar && /tmp/buster-dependency-scalar --oracle
```

On an AVX-512F/BW host:

```sh
clang -std=c17 -O2 -march=native -fwrapv -funsigned-char -fno-strict-aliasing -Wall -Wextra -Werror tests/driver/dependency_rule.c -o /tmp/buster-dependency-native && /tmp/buster-dependency-native --oracle
```

```sh
clang -std=c17 -O1 -g -march=native -fwrapv -funsigned-char -fno-strict-aliasing -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=all tests/driver/dependency_rule.c -o /tmp/buster-dependency-sanitized && /tmp/buster-dependency-sanitized --oracle
```

Coverage includes both paths against an independent quoting implementation over
8,192 generated filenames, every byte value at guarded classifier tail lengths
0 through 64, full rules ending at a protected page, exact/insufficient output
capacities, malformed early and late records, system filtering, phony rules,
explicit target order, and size overflow. Standalone success is not a compiler
build, a CLI regression pass, or a performance qualification.

## Integration and remaining qualification

The driver parses dependency options without reinterpreting operands as actions,
collects semantic file observations with inherited system provenance, and publishes
complete per-unit buffers in command-line order after successful compilation.
Diagnostic replay and speculative cache opens do not publish dependencies.
The CC guide records naming, aliases, publication failure behavior, feature limits
and the broader #1113 work that remains. This does not implement `-imacros` or
finish #805's entire diagnostic/linker compatibility wishlist.

Full source-corpus and native 9700X qualification are distinct from the standalone
serializer and bounded CLI tests. No performance conclusion follows from these
functional checks.

## Provenance

The implementation and fixtures are original project C under
`Apache-2.0 WITH LLVM-exception`. GCC's
[Preprocessor Options](https://gcc.gnu.org/onlinedocs/gcc/Preprocessor-Options.html)
and external GCC/Clang executions provide behavior facts only. No upstream
implementation, test fixture, or generated metadata is imported.
