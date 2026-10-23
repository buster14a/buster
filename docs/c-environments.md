# Source-position environments for deferred bodies

`c_environment.c` supplies the immutable environment boundary for
[#113](https://github.com/buster14a/buster/issues/113). It retains file-scope
declaration history, answers scalar and batched position queries, checks tag
completion at a use's position, and extracts source-ordered body ranges from
the validated parser output. It does not turn on deferred checking in the
compiler. The source adapter and checker are being developed in
[#312](https://github.com/buster14a/buster/pull/312), under #128 and #133.

## Producer, handoff and consumer

The top-level reader must parse and check every declaration, initializer, tag
definition and enum definition. It emits `CBindings` declarations with their
semantic scope depths and activation positions. A declarator activates after
its complete declarator and before its initializer; an enumerator after its
definition; a tag after its name. Tokens and positions belong to one complete
**preprocessed** stream. File byte positions, brace depths, presumed line
numbers, intern IDs and tag-completion ordinals are not source positions.

After this ordered phase, `c_environment_build` counts, places and fills an
index of declarations at semantic scope depth zero, in the ordinary and tag
namespaces. Declarations in initializers are included. Parameters and local
declarations, including block-scope `extern`, are excluded. Labels and members
have their own function/type scopes. Prefix sums over the name counts use
AVX-512; filling each name's history retains the original activation order.

The index owns offsets and two columns per file-scope declaration: activation
point and original activation handle. It borrows the `CBindings` stream.
Allocation failure empties the result and rolls back the output and scratch
arenas; the two arenas must be distinct and must not own the input columns.
Scratch is released on success too. The complete index and source events must
remain alive and unchanged until all readers join; resetting or appending to
the source during that phase violates the handoff contract.

A deferred body first consults its private local binding stacks. When those
do not contain the name, `c_environment_lookup` answers the file-scope query
at the **use's** token position. It binary-searches the name's history for the
last declaration activated at or before that point. A later redeclaration
cannot replace that result. An activation handle maps through
`bindings.declaration[handle - 1]` to the original declaration token; the
consumer must keep the type of that declaration, rather than overwrite it
with the entity's final composite type.

`c_environment_lookup_batch` searches up to 16 histories together. Its inputs
are `count` keys and use positions; its output is `count` activation handles.
A key is `namespace * environment.names + IdentId`, where the namespace is
ordinary or tag and the identifier is nonzero. A valid but undeclared name
gets zero. An invalid key fails the batch, whose output must be discarded.
The scalar public lookup also gives zero for invalid names. Binary-search
candidate loads are scalar, with AVX-512 comparisons and updates; there are
no hardware gathers or scatters.

Every input/output column is exactly its logical count, without required
padding. The query columns and output must be disjoint from one another and
the index. The caller may flush any partial batch, change batch partitions,
or query bodies in any order. Empty batches succeed; queries allocate no
memory. Scope-event production and full ordinary/local name resolution remain
with #128: its `c_bindings_scalar`/`c_bindings_batch` twins give the resolved
declaration column for all uses, including locals and exact same-position
event ordering. This index does not replace that pass.

## Completion by position

The type producer retains an additional `uint32_t` column indexed by
`TypeTags` index. A completed tag has the token position at which it became
complete; an incomplete tag has `UINT32_MAX`. Normally that point is just
after the closing brace. The language reader supplies earlier completion
points where its language rules require them. Position zero is valid.
`TypeTags.completion` remains its existing **ordinal**, and is not used as a
token cutoff.

`c_environment_types` and its scalar twin take a read-only type table,
completion column, TypeIds, use positions, and one result byte per use.
They include preparation of the completion requirement: tags use their own
identity, array/atomic/aligned/vector/complex wrappers inherit the contained
object's requirement, and pointers are complete independently of the
pointee. A completed record's own completion already implies its members'
requirements. An incomplete type or missing tag position fails closed.
The checker still decides which operation requires a complete object type.

For completeness queries, the position is where the operand/type-name has
finished being read. For example, `sizeof(struct S { int x; })` completes `S`
inside its operand and must succeed. Supplying the prefix `sizeof` token's
position would incorrectly reject it. Identifier binding queries retain the
identifier's own use position. The adapter must preserve this distinction
rather than substitute the body's end or the end of the translation unit.

The input type table must have been constructed by the validated type APIs.
Workers may not complete or derive types in this shared table. Types created
inside a body need private storage and stable source identities; publishing
or remapping those results belongs to the checker integration. Declaration
type histories and tag-completion histories are both necessary: a single
final symbol table or a single final `type_is_complete` query is insufficient.

## Body ranges and ordered results

`c_environment_bodies` reads the validated parser's `OPEN_BODY` and
`CLOSE_BODY` roles and matching depths. Other brace roles are not function
bodies. Each output range includes both braces, `[begin,end)`, and its array
index is the body's source-order result slot. The parser has already done
the bracket matching; neither source bytes nor an independent brace skim
are used. The caller provides range capacity. Invalid pairings or exhaustion
fail and set the output count to zero.

Workers own their slots and their local arenas. After joining, consume slots
in source order. Buffer diagnostics through the existing `DiagnosticJob` and
`diagnostic_merge` interfaces, and retain literal first-use positions. A
block-scope linkage declaration or C89 implicit function declaration must
produce a body-local effect record; the checker must reconcile those records
and composite types in source order after joining. The environment index
does not itself introduce file-scope visibility for those block-scope names.
It supplies no scheduler, IR merger or composite-type effect merger.

## Tests and measurements

```sh
./build/compiler test-environments [--seed N]
./build/compiler benchmark-environments [--names N] [--history N] [--queries N]
./build/compiler benchmark-environments --only full-vector
./build/compiler benchmark-environments --only full-vector --after-scalar 100
```

The tests compare both lookup paths with a separate linear-history oracle
on random event worlds, including repeated activation points, restored file
heads, local shadowing and separate namespaces. Queries run in reverse order
at 1, 2, 3 and 8 lanes with several batch partitions. Type fixtures distinguish
tag identities and check later completion through pointers and derived types.
Guarded inputs/outputs cover every logical length from zero through 97;
allocation failures check rollback and scratch release.

Six C fixtures run through preprocessing and both real parser paths, with
Clang separately checking acceptance of the C visibility rules. They cover
the issue's examples, typedef shadowing, a block `extern`, macro-generated
bodies, and nested initializer/tag braces. **Semantic events in these tests
are supplied explicitly**, rather than extracted by a production adapter.
These tests establish the boundary's behavior, not completed source checking.

The synthetic benchmark reports index construction, scalar/vector lookups,
the complete index-to-query handoff, and scalar/vector completeness checks,
including TypeId dependency walking. Each measured output is compared with
its scalar reference. It reports 21-pass medians of ns and hardware cycles
per item, retained index bytes and construction scratch. Unavailable or
multiplexed counters are reported as unavailable. The default synthetic
workload has 4,096 identifiers, four generations of declarations and 65,536
queries. Source extraction, parsing, type construction and checking are not
in those timings. Use the 9700X shared lock for each build/test/measurement
separately, and pin measurements to CPU 2. Run isolated vector measurements
both sustained and following lighter work across independent processes.

## 9700X observations, October 23, 2026

[CI run 39933440023](https://github.com/buster14a/buster/actions/runs/39933440023)
passed for PR head `b43576c127a2b9fd41421d5b6b274c341e4f8ea7`. The actual
tested synthetic merge was `09cb0021ddc5bc290a20d0f05effccf74c3e01c0`, combining
that head with main `635434f2a4d153c6de1dd2f1149055c4507c6f46`. This is hosted
CI evidence, not a merge of the PR. The approved physical Ryzen 7 9700X ran
Clang 23.1.1 with the performance governor; the environment measurements were
pinned to CPU 2 under the runner's shared lock.

The environment suite passed **420,202 checks, zero failures**. All existing
test commands and the call-graph gate passed too: 32 translation units,
16,644 functions, zero unapproved cycles and three allowed components.
Separate sanitizer validation remains pending.

The first benchmark process interleaved all paths over 16,380 declarations,
4,096 names and 65,536 queries, with a warmup and 21 measured passes:

| Path | ns/item | hardware cycles/item | Item |
| --- | ---: | ---: | --- |
| Index construction | 1.715 | 9.522 | declaration |
| Scalar lookup | 8.164 | 45.247 | query |
| Vector lookup | 5.371 | 29.709 | query |
| Index plus scalar lookup | 8.693 | 48.207 | query |
| Index plus vector lookup | 5.804 | 32.180 | query |
| Scalar completeness | 1.058 | 5.864 | query |
| Vector completeness | 0.998 | 5.531 | query |

The retained index used 163,892 bytes. Construction required 32,768 scratch
bytes plus alignment; query scratch is bounded on the stack. Source event
extraction and type construction were outside the timing.

Three further independent processes per path/state gave these ranges of
process medians. Each process used the same generated inputs and 21 passes;
the conditioned state ran scalar lookups for 100 ms before every sample:

| Vector path/state | ns/query range | cycles/query range |
| --- | ---: | ---: |
| Index plus lookup, sustained | 5.166–5.387 | 28.631–29.865 |
| Index plus lookup, after scalar | 5.701–6.182 | 31.598–34.279 |
| Completeness, sustained | 0.985–0.997 | 5.465–5.531 |
| Completeness, after scalar | 0.998–0.999 | 5.534–5.540 |

These ranges describe observed process medians, not confidence intervals.
Every measured result matched the scalar reference. Source-corpus and
whole-compiler performance acceptance remain separate requirements.

The same CI run's `main.c` parser measurement covered 2,291,368 tokens in
21 interleaved passes. The scalar parser reported 24.958 ms, 10.892 ns/token,
60.001 cycles/token and 295.241 instructions/token; the vector path reported
13.391 ms, 5.844 ns/token, 32.329 cycles/token and 60.913 instructions/token.
Its fallback covered 58 top-level declarations and 33,911 tokens (1.480%).
The environment numbers above are synthetic per-query costs. Measuring their
added cost in the source parser/checker pipeline requires the adapter below.

## Remaining acceptance for #113

- Connect the #128/#133 source adapter to the immutable file environment,
  per-declaration type identities, and tag-completion-position producer.
- Check every top-level initializer and its scope effects before deferring
  bodies; keep body-local type construction private.
- Implement source-ordered linkage/composite-type effects and connect actual
  typed streams, IR, diagnostics and literal outputs to body result slots.
- Compare real sequential and out-of-order checking, both resolver paths,
  one and eight lanes, on all issue fixtures, SQLite, libc and c-testsuite.
- Measure position-check cost with the parser on those frozen inputs on the
  locked 9700X. Synthetic timings do not discharge that requirement.

The implementation, fixtures and documentation are original project work.
Clang is run externally as an oracle; no compiler implementation or external
test corpus is copied into this change.
