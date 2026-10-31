# C-source IR text round trips

`test-ir-source` checks the text-format acceptance point of
[#175](https://github.com/buster14a/buster/issues/175), in addition to
[#131](https://github.com/buster14a/buster/issues/131)'s lowering and
[#134](https://github.com/buster14a/buster/issues/134)'s promotion checks.
Its command line is unchanged:

```sh
./build/compiler test-ir-source [--print NAME] [preprocessor options] file...
```

## The gate

For each function that the source producer prepares, the existing scalar and
vector typed-stream lowerings must agree. The command prints and parses the
lowered form, the vector-promoted SSA form, and the scalar-promoted SSA form.
Each representation goes through **both** text parser paths. Every parse must
consume the entire printed function, pass the parser's full verifier including
dominance, and reproduce the same canonical bytes when printed again.

The structural comparison is independent of the printer. It compares instruction,
block, slot and label identities; instruction flags and memory alignment; constant
bits and resolved sparse operands; phi predecessors and values; switch cases;
direct and indirect calls, signatures and aggregate ABI records; function
parameters, aggregate returns and variadic incoming-frame metadata. It does not
infer structural equality merely from matching printed text.

The comparison permits only the normalization documented in
[the text parser contract](ir-tools.md#parser-contract): unused sparse rows and
sharing need not survive, symbol capacity is not serialized, an unindexed address
has no observable scale, and storage addresses, allocation charges and padding
are not format properties. Sparse rows are compared through their uses rather
than by incidental pool indices. Inactive function ABI fields are not serialized
either; their presence is governed by `parameter_passed` and `variadic`.

A round-trip failure identifies the source file, function, representation, stage
and parser diagnostic. `text_forms` counts representations attempted, each through
both parser paths; `text_failed` counts failed representations. A nonzero failure
count makes the command fail. On a completely accepted run, `text_forms` is three
times the function count and `text_failed` is zero.

## Checking the checker

Before the source files, the command reads the existing arithmetic, conversion,
control-flow, memory/call, dynamic-stack, label, aggregate, SIMD, empty-function
and Tier 0 text fixtures. Two separately parsed copies must agree. Intentional
changes to operands, constants, metadata, memory attributes and ABI fields must
be detected, and undoing each change must restore equality.

An independently constructed function exercises shared constants and calls,
unused sparse entries, an oversized translation-unit symbol bound and an
unindexed scale. Its round trip must preserve meaning without preserving those
incidental details. An exhausted output arena must fail with the parser's memory
diagnostic, retain its prefix, and return its temporary storage. The command
reports the oracle's check and failure counts separately.

## Lifetimes and integration

The helper is `src/buster/compiler/ir_source_roundtrip.c`, included after
`ir_parse_test.c` by `ir_source_test.c`. It is scalar test code, not a compiler
execution path or a benchmark variant. A separate output arena owns the two
parsed views of one representation. Both views and all parser scratch are
released before proceeding; printed buffers are freed on every path. Memory
streams avoid one temporary disk file for every printed function.

The existing correctness manifest already runs `test-ir-source` on c-testsuite's
declared subset and the frozen SQLite and musl sources. Those invocations now
exercise the text gate without changing the workflow or its execution permissions.
The source command retains its existing explicit `left_out` accounting for
unsupported source files; this change does not turn those exclusions into passes
or establish support for every C feature. Lua can use the same command but is not
part of those three existing manifest invocations.

The new acceptance run must be recorded against the exact tested commit on the
approved 9700X. Implementation and static review alone do not establish a corpus
pass, execution equivalence, Tier 1 completeness, or the throughput targets of
[#115](https://github.com/buster14a/buster/issues/115). Raw logs and measurements
belong in `buster14a/buster-performance`; link their evidence rather than adding
them under `docs/measurements` here.

The helper, negative tests and documentation are original project material under
Apache-2.0 WITH LLVM-exception. They reuse project fixtures and public IR/parser
interfaces; no external implementation, corpus or generated data is imported.
