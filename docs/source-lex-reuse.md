# Bounded raw source reuse

Owning issue: [#1470](https://github.com/buster14a/buster/issues/1470).
Baseline: `c5a073139691e9111986c4fca6f6b03d2a6dfcf1`, tree
`68adbe94cf6bd2c00671c4e413552d54791fd1af`. Implementation is C only,
dependency-free, default-off, and keeps the ordinary canonical IR pipeline.

## Decision and existing work

Reuse **raw source translation and lexing before symbol interning**. It is
a small boundary whose sole semantic input is one captured raw byte sequence.
The root and resolved-include seams in `c_preprocess_run` use
`c_source_cache_lex`; synthesized macro/pragma text and the independent
`c_lex_reference` remain on their existing paths.

Current include identity tables already suppress imports, pragma-once and
proven whole-file guards. Macro definitions already retain their spacing,
parameter and paste/stringify facts. They remain authoritative and fresh.

| Alternative | Decision for this increment |
|---|---|
| Persistent AST | Arena-pointer ownership, declaration-order binding and a historically small parsing share do not justify the first implementation. |
| Cached preprocessed headers/expansions | Must replay macro and pragma side effects, rescan state, source stamps, include origins and negative queries; #1107 already rejected a final-expansion cache. |
| Canonical function IR | Lowering interns program-wide types and symbols. Replaying those mutations soundly is unresolved. |
| Encoded function contributions | #1471 retained research only: historical cold overhead about 20%, edited self-host rebuilds within noise, around 77 MB packs. Do not revive it as an accepted optimization. |
| Whole TU/result after fresh preprocessing | Could skip more on unchanged builds, but needs a complete downstream-option/provenance/diagnostic key and artifact lifetime/publication protocol. Larger than this increment. |
| Daemon | A process lifecycle choice, not a dependency boundary. No daemon is needed here. |
| Raw lexical templates | Chosen: context-independent equality proof, bounded owned data, small integration surface. Net speed economics remain pending. |

Historical measurements above belong to prototype `d5616c6b` and its
earlier main, not the current baseline. A raw lex cache does not skip semantics
or backend work and has no cross-process persistence. Its strongest expected
use is shared headers in serial multi-input compilation and repeated embedding
calls; repeated single-input CLI commands receive no warm process-local state.

## Use and ownership

`ide cc -fsource-cache -c a.c b.c` requests an invocation-local 16 MiB
payload cache. It is released on every success/error exit.
`-fno-source-cache` cancels that request; the last flag wins. The default
is disabled. `-v` adds a versioned `SOURCE_CACHE` record. The SOURCE
metrics keep their conceptual input counts; cache counters identify work
physically avoided. An optional reservation failure uses the ordinary path.

Embedding callers create a cache with
`c_source_cache_create(owner, byte_limit)`, pass it through
`CompilerDriverInvocation.source_cache` or
`CPreprocessOptions.source_cache`, then call `c_source_cache_destroy`.
Limits accept nonzero budgets up to 64 MiB. Metadata remains in the caller's
owner arena; the private payload reservation is destroyed separately.
`c_source_cache_clear` discards entries but retains cumulative counters.
The metadata owner must outlive the cache's last use, and callers destroy the
cache before destroying that owner.

The cache has one exclusive creating-thread owner. A driver invocation with
a cache clamps TU workers to one; embedding callers must not share one cache
between threads or nested concurrent invocations. No locks, callbacks,
background worker, file database or second IR is introduced.

| Data | Owner / last reader |
|---|---|
| Metadata and fixed 64-entry table | Caller owner arena / cache's last use |
| Captured raw key, compact pristine lex template | Private bounded cache arena / eviction or destruction |
| Per-call immutable raw snapshot | Separate scratch scope / current lexical lookup and cold capture |
| Imported tokens, shapes and checkpoint arrays | Current preprocessing phase / seal |
| Imported translated spellings | Current spelling arena / TU downstream completion |
| Sealed source maps, diagnostics, AST and canonical IR | Existing TU ownership / downstream completion |

Imports deep-copy every pointer-bearing lexical component. Token offsets are
normalized on capture and rebased into the current spelling space on import;
symbols remain zero until **fresh** interning. Replay preserves the cold raw-size
spelling reservation so splices/CRLF folds do not shift subsequent offsets or
conceptual spelling-byte metrics; the retained template remains compact. Checkpoints and their offsets
are file-local and copied unchanged; the location cursor resets. EOF and the
translated terminator are retained. Nothing in a result or canonical IR points
into the cache. A cache may be cleared or destroyed while an earlier result
is still alive.

## Input and dependency model

Let `L(B)` be the deterministic translated source, pristine token/shape
rows, source checkpoints/pages and metrics from raw bytes `B`.
The cache substitutes an owned copy of `L(B)` only after **full exact byte
equality** with its immutable retained key. The fixed-size table searches
length/bytes directly: neither hashes nor filesystem metadata admit hits.

Each enabled lookup first checks the ordinary raw-size and shared 32-bit
spelling-space limits, then takes one owned raw snapshot. Lookup, cold lexing
and insertion all use that snapshot. Comparing a live mmap and later lexing
it would permit concurrent edits to poison a template; the snapshot avoids
that inconsistency. Capture itself may observe a concurrently written file
as any read can; the captured bytes are the actual compilation input.
No atomic multi-file filesystem snapshot is promised.

The rest of compilation is `F(L(B), C)`, with context `C` rebuilt for
each compilation:

| Input / dependency | Fresh consumer and invalidation behavior |
|---|---|
| Root bytes and included-file bytes | Capture after each current read/resolution. Same-length edits miss by byte comparison; spelling paths, inode, mtime and size are never validity shortcuts. |
| Ordered -D/-U operations, macro definitions, expansion generations, push/pop and paste | Fresh preprocessing and interning. No final expansion is retained. |
| Ordered quote/system include paths, source directory, sysroot and include_next origin | Fresh resolution at each directive/query. No positive or negative resolution is cached. |
| Previously missing file becomes present, nearer file shadows an old match, deletion or alias change | Fresh open/search and descriptor identity capture; then the selected bytes may reuse lexical work. |
| Conditional compilation and __has_include / target capability queries | Fresh evaluation, including false branches and query dependencies. |
| Target, CPU/features, data layout, ABI options, plain char, dialect, optimization/PIC/debug/allocator options | Fresh target macros, semantics, canonical preparation/validation and backend. Raw lexing takes none of these inputs. |
| pragma once/import/guards, pack, push_macro/pop_macro and _Pragma | Fresh identity tables and ordered effects. A lexical hit does not suppress execution. |
| __FILE__, __LINE__, logical #line paths, include depth and other supported contextual operations | Fresh frame/source-map association and macro expansion. No source path or TU-local identifier is retained in the template. |
| Context-sensitive semantic builtins and function names | Fresh semantic analysis/lowering. Unsupported builtins retain ordinary diagnostics. |
| Time and filesystem inputs | Baseline __DATE__/__TIME__ explicitly use fixed epoch strings; command overrides run freshly. __TIMESTAMP__/__COUNTER__ are not newly implemented. If actual clock/environment inputs are added, evaluation stays above this boundary and replay must capture them identically. |
| Compiler implementation/schema | Process-local only; a new compiler process starts empty. There is no serialized format or cross-binary artifact admission. |

Lexical diagnostics bypass insertion and run fresh. Oversize entries, exhausted
representable spelling space, disabled/unavailable/destroyed caches and empty
or invalid budgets use ordinary lexing. Capacity pressure discards the entire
generation; this may reduce hits but cannot retain stale data. A downstream
error does not invalidate a clean raw template because all downstream work runs
again. Changes to any semantic context require fresh downstream compilation,
which this implementation always performs; no conditional partial rebuild
decision is needed.

## Soundness argument

1. Lexing's semantic inputs are the captured bytes. Its host SIMD and scalar
   paths already have an independent equivalence contract.
2. Exact raw equality admits only the same `L(B)`. No path, timestamp,
   fingerprint or symbol-ID shortcut weakens equality.
3. Imported data is pristine, fully owned by current phase/spelling arenas,
   and offset rebasing preserves each spelling and local source checkpoint.
4. The existing preprocessor attaches all current context, interns fresh IDs,
   executes every current effect and seals the current graph normally.
5. All later frontend, canonical IR validation, native/non-native backend and
   artifact publication operations are unchanged.

Thus reuse replaces only recomputation of `L(B)`, not any computation that
depends on `C`. Memory pressure and lexical errors conservatively recompute.
This argument does not assert absence of existing compiler bugs; fresh
compilation is the oracle for incremental behavior.

## Replay and evidence contract

The registered driver replay fixture runs edit sequences through a shared
cache and through fresh compilation with identical input/output paths, flags
and stable fixture files. It compares status, structured diagnostic fields
and rendered text, warnings, and object bytes. Cached bytes are read before
the fresh invocation overwrites the same path. No artifact or diagnostic
field is waived as nondeterministic; only cache-work counters differ.

Sequences exercise macros and repeated unguarded headers, command-operation
order, conditional/query changes, missing-to-present and search-shadowing
files, reordered include paths and include_next, pragmas, physical/logical
source locations, target/ABI differences, CRLF/splices, malformed edits and
recovery, bounded eviction/bypass, and destruction while results remain alive.
Tests must prove hits occur while current diagnostics/artifacts still agree;
a cache silently disabled for every step cannot pass the reuse assertions.

Baseline correctness was executed on GitHub-hosted runners:
[full exact-SHA CI](https://github.com/buster14a/buster/actions/runs/36909467868)
and [strong self-host audit](https://github.com/buster14a/buster/actions/runs/36912817918).
The latter records `5033805/5033805` assertions, a 41,121,024-byte fixed point,
three generations/two repetitions and 432 independent probe configurations
without failures. These are baseline results only. Candidate results, exact
head and unresolved failures are recorded on #1470 and the implementation PR.

## Work and bounded costs

A hit avoids phase-1/2 scanning, checkpoint/page construction, lexical scanning,
metric branch updates and worst-case lexical row reservations. It still pays
one raw capture, bounded exact comparisons, compact copies and token rebasing,
then all interning, masks, preprocessing, parsing, semantics, lowering,
validation, backend and output work.

A cold lookup additionally pays raw snapshot and compact persistent capture.
No hash is computed. Null caches pay only the disabled branch at the two
source seams. Payload use includes allocation padding and never exceeds the
requested limit. Sixty-four entry descriptors are fixed metadata; reservation
and initial commitment add an arena/header/page allowance of 64 KiB.
Maximum caller-requested payload is 64 MiB; CLI uses 16 MiB. Reset does not
grow the reservation. Peak phase/TU memory remains its ordinary demand plus
one captured raw file at a time in a scratch scope rewound before each helper
returns (at most the payload budget; insufficient scratch capacity bypasses). The payload and metadata remain bounded;
this is not a bound on total compiler/TU memory.

## Qualified-host recipe (acceptance pending)

Do not execute this recipe on the laptop or an ordinary hosted runner as
performance acceptance. Arrange a separately authorized qualified-host session;
this task performs no benchpress/9700X operation. Use the trusted Clang-built
Release compiler, fixed sources/flags/target, record source/tree/compiler image
and environment identities, and preserve raw observations.

For serial batch economics, construct a response file with repeated small C
inputs sharing a representative header closure (distinct object output names),
and a matched no-sharing batch. Use the same frozen manifest and freshly
prepared output directory for both modes:

```sh
./build.sh generate --cc clang
./build.sh build --config Release -t ide
build/Release/ide cc -fno-source-cache -c @batch.rsp
build/Release/ide cc -fsource-cache -c @batch.rsp
build/Release/ide cc -v -fsource-cache -c @batch.rsp
```

The third command is a separate counter/diagnostic run, not a timed sample.
Time complete compiler processes with the qualified harness, using paired
ABBA order for at least 10 pairs. Collect wall time, CPU time, peak RSS and
object digests; compare cold capture/no-sharing and shared-header batches.
All CLI samples start with empty caches; reuse occurs within the batch.

For persistent embedding economics, use the same driver API and fixture-edit
replay protocol: fresh/no-cache, empty-cache, unchanged warm, one root edit,
shared-header edit, macro/target changes and deliberate capacity thrashing.
Give each timed arm its own equivalent captured fixture state; recreate its TU
arena each iteration, keep only the explicitly owned cache across warm calls,
and exclude setup/fixture writes identically. Record hit/miss/bypass/reset,
reused raw bytes/token rows, retained payload and full artifact/diagnostic
equality beside complete-invocation wall/CPU/RSS. Report costs even when slower.

Predeclare useful-workload and worst-case cold/RSS budgets before seeing
samples. Do not enable by default on counters alone. Qualified net speedup,
cold overhead and RSS acceptance remain **pending**.

## License verification

No external project or implementation was researched or copied for this
increment. First-party Buster licensing remains explicitly unspecified and
unresolved at the baseline, verified in
[LICENSES/README.md](https://github.com/buster14a/buster/blob/c5a073139691e9111986c4fca6f6b03d2a6dfcf1/LICENSES/README.md)
and tracked by #621. Preserved third-party notice documents do not grant a
first-party license.
