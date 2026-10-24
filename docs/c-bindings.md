# Scoped C binding kernels

`c_bindings.c` implements the resolution kernels for
[#128](https://github.com/buster14a/buster/issues/128).
`c_parser_binding_trace.c` connects the scalar grammar's declaration and scope
transitions to those kernels; `c_parser_bindings.c` exposes the source adapter
and declaration-token columns. K3 now classifies block-scope typedef names
using per-identifier activation stacks. The syntax-only driver does not need
name-resolution output and does not run the optional adapter. The checker
records and resolves its own events while elaborating, and uses the adapter
only as an oracle and a measured alternative
([the checker's binding handoff](c-check-bindings.md), #336).

## Event contract

A `CBindings` belongs to one compilation thread. It has three dense head
arrays indexed by `IdentId`: ordinary names, tags, and labels. Declaration
activations are columns of name/namespace, declaration token, point of
declaration, scope depth, kind, previous binding, and visibility end. An
activation handle is its event index plus one; zero means unresolved.
The caller owns the arena and chooses explicit identifier and event capacities.

The parser emits events in visibility order:

1. `c_bindings_mark` saves the undo-log length at scope entry.
2. `c_bindings_declare` activates a declaration. A declarator becomes visible
   after its **complete declarator**, before its initializer. An enumerator
   becomes visible after its definition, and a tag after its name. This
   distinction matters inside an array bound: `typedef int T;` followed in
   a block by `int T[sizeof((T)0)];` still uses the typedef in the bound.
3. `c_bindings_use` records a use. `c_bindings_lookup` also answers the
   parser's immediate typedef question in one head-array load; the binding's
   kind tells it whether this is a typedef. Specifier/declarator context still
   belongs to the parser.
4. `c_bindings_restore` restores a saved undo mark, recording the exclusive
   end of each activation. Marks also express selection/iteration scopes and
   their substatements; braces alone are insufficient.
5. `c_bindings_reset` pops any remaining file-scope bindings and reuses the
   heads for the next unit without clearing every process-wide identifier.

The declaration token and point of activation are separate. A definition's
parameters can be deactivated at the prototype's end and reactivated at its
body with their original declaration tokens. This leaves them hidden from
the remaining return declarator. Multiple events can share one token position;
their event indices preserve the exact before/after order.

Labels must be predeclared for their whole function before its goto uses are
recorded, and restored at the function boundary. Their declaration tokens may
follow their uses in source. A parser adapter can collect label definitions
from the completed role tree before emitting that function's events. GNU local
labels need their own scope decisions. Member names need a containing type and
remain the type checker's responsibility.

The public stream-building functions reject out-of-range identifiers,
namespaces, kinds, backwards activation points, invalid undo marks and
exhausted capacities. Failure is sticky: the partial unit must be rejected,
then reset. The caller must discard output on a failed resolution. A failed
constructor and both complete resolution entry points restore their arena
marks. Separate sort/scan helpers expose scratch lifetimes to the benchmark.

## Algorithms

The streaming stack is the parser's immediate lookup path. An independent
`CBindingReplay` replays the events with fresh heads, its own previous-binding
column and an explicit undo stack. It never reads the producer's previous
bindings or visibility ends. Its heads are cleared once when allocated and
remain reusable because replay pops the file scope before returning.

The batch path packs a 64-bit key per declaration/use: namespace and identifier
in the high 32 bits, event index in the low 32. Namespace is encoded as
`namespace * name_capacity + IdentId`, with an overflow check at construction.
Input is already ordered by event index, so a stable byte radix sort only sorts
the high half. While packing keys, `c_bindings_sorted` accumulates the bitwise
OR and AND of their names; their XOR identifies every bit that varies. The
vector sort omits any byte pass with no varying bit. This removes work even
when constant bytes are nonzero or lie between changing bytes. The scalar
oracle still runs all four passes. Sixteen keys at a time use `vpconflictd` and population
counts for the lane's bucket rank. Only the last lane for each bucket updates
the histogram or cursor. Scatters are scalar stores to computed addresses.
There are no hardware gathers or scatters.

The internal producer/consumer contract is explicit: `c_bindings_sort` receives
the variability mask with the packed event count and returns the buffer that
holds the result. An odd number of passes ends in the temporary buffer, so
`c_bindings_sorted` hands that buffer directly to `c_bindings_scan` rather than
copying it back. Both nonoverlapping buffers remain owned by the same scratch
arena until the consumer finishes. Neither requires padding. Empty and
one-record inputs take no radix pass; all-constant names retain event order
without touching the temporary buffer. Allocation sizes and failure handling
are unchanged. The mask may conservatively include extra bits, but must include
every bit that differs between retained keys.

A segmented inclusive maximum scan over sorted declaration indices gives each
use its latest declaration candidate. The scan carries the last segment across
blocks. Each candidate is checked against its visibility end. A closed scope
walks to the preceding declaration of the same name; after eight unsuccessful
candidates, a lazily computed scalar replay resolves that use. The replay is
computed at most once per batch. Statistics count inspected links, uses that
walked, and uses that reached the fallback. This bounds pathological chains of
closed sibling scopes without trusting results from the stream producer.

All loads are bounded, including partial blocks ending at an inaccessible
page. A result is an activation handle; `declaration[handle - 1]` maps it to
the original declaration token. The parser adapter must map use events back
to its token columns; duplicated parameter activations are not extra source
identifiers.

## Validation

`./build/compiler test-bindings [--seed N]` checks:

- fixtures for typedef/object shadowing, independent tag and label namespaces,
  visibility before initializers and after enumerators, definition parameters
  with disjoint visibility, forward labels, unresolved uses and file-scope
  cleanup;
- 1,000 generated scope programs per seed, comparing streaming lookup, a
  reusable replay, a fresh replay, and the batch against a separate live-list
  oracle;
- 100,000 nested bindings, including the bounded fallback after scopes close;
- the radix sort against its scalar twin, all lengths from 0 through 96 with
  both buffers ending at protected pages, all-equal and repeated buckets,
  full-width random names, and every subset of varying name bytes with nonzero
  constant bytes. These cover zero through four passes, odd-pass output in the
  temporary buffer, and stable event ordering;
- empty streams, invalid inputs, event exhaustion, intermediate allocation
  failures and insufficient scratch during the lazy fallback.

These are **event-stream** tests. The source tests below independently check
the adapter's grammar decisions and original declaration-token identities.
The 9700X workflow runs both suites.

`./build/compiler benchmark-bindings [--events N] [--names N]
[--only scalar|batch|sort] [--after-scalar MS]` measures a deterministic
**synthetic** stream, with preparation outside timing and reusable scalar
heads. It reports 21 interleaved-pass medians of nanoseconds and hardware
cycles per identifier event, the sort's cost, and candidate walks. Every
resolution is compared with the stack replay. Unavailable or multiplexed
cycle counters are reported as unavailable. `--only` isolates a path;
`--after-scalar 200` measures it after lighter work. Each target-host invocation
takes `~/buster-fast-bench/exclusive.sh` separately and is pinned to CPU 2.

### Constant-digit elimination: cloud measurements, October 23, 2026 (#301)

The retained change was compared with `1f1e116ad358bed8d5a97824b916908d7cb44e96`
on a cloud AMD EPYC 9V74, Ubuntu Clang 18.1.3, pinned to CPU 2 with one
invocation holding the common experiment lock at a time. These are synthetic
event-stream measurements on a different host from the 9700X. They do not
establish parser integration or a complete compiler throughput gain.

The final complete compiler executables each ran the existing default
`benchmark-bindings` in three independently started, alternating processes.
Each process reports the median of 21 passes over 246,065 identifier events
from 262,144 generated events with 8,192 possible names. Both low name bytes
vary, so two of the four radix passes remain. Timings include key packing;
the batch also includes resolution and scope checks. Median process times
and their minimum and maximum, in ns/identifier, were:

| Path | Baseline | Constant-digit elimination |
| --- | ---: | ---: |
| Sort and packing | 10.859 [10.846, 11.846] | 7.986 [7.589, 8.727] |
| Complete binding batch | 29.978 [27.539, 31.490] | 24.100 [22.442, 27.630] |

The sort median falls by 26.5%, and the batch median by 19.6%. All three
paired processes improve, although the batch ranges overlap. Earlier isolated
`--only sort` and `--only batch` runs through a standalone entry point gave
24.4% and 9.6% reductions respectively, illustrating the cloud timing spread.
Every measured output matches the reusable scalar replay and retains the
same scope-walk and fallback counts.

Three-process standalone checks with `--names 2`, `256`, and `1000000`
exercise zero, one, and three varying bytes. Their sort medians fall from
7.220 to 1.053, 9.737 to 4.660, and 12.927 to 10.600 ns/identifier,
respectively. These are controlled distribution checks, not estimates of
real-source identifier distributions. The protected-page tests also cover
all four varying bytes and every subset of constant middle bytes.

The expanded suite passes 4,255,777 checks, including AddressSanitizer and
UndefinedBehaviorSanitizer with leak detection disabled because the sandbox
blocks LeakSanitizer's process inspection. The
[measurement record](https://github.com/buster14a/buster-performance/blob/a00d16e47b6f1090b8a0424ab0dd8cb4e6016713/docs/measurements/simd-301-2026-10-23/README.md) preserves
raw process output, source and executable identities, commands, and limits.
9700X qualification remains separate.

## Source adapter and consumer contract

After a successful `parse_translation_unit`, call
`parse_bindings_build(&bindings, &parser, &output_arena)`. It produces a
`ParseBindings` with the reusable `CBindings` event stream and these columns:

| Column | Meaning |
| --- | --- |
| `token_events[t]` | Activation/use event index plus one; zero for tokens outside the ordinary, tag and label namespaces. |
| `declarations[t]` | Original parser token declaring the visible name, or `UINT32_MAX` when unresolved or declared by a builtin without a source token. |
| `namespaces[t]` | `C_BINDING_ORDINARY`, `C_BINDING_TAG` or `C_BINDING_LABEL`; `UINT8_MAX` for a nonbinding token. |
| `kinds[t]` | Typedef, object, function, enumerator, tag or label. A function-style parameter is an object after C's parameter adjustment. |

An explicit declaration maps to its own token. Same-scope tag redeclarations
remain separate lexical declaration records; the type checker can join their
entities through the event's `previous` link and `depth` scope identity.
`struct S *p` reuses a visible tag and introduces one only when none is visible.
Member declarators/accesses/designators, attribute names and asm operand
symbols are outside these namespaces; member lookup belongs to a containing
type. Undefined ordinary names and labels have a nonzero use event and a
`UINT32_MAX` declaration; diagnosing them belongs to semantic checking.

`parse_bindings_resolve(&bindings, scalar, &scratch)` independently resolves
the same stream and rewrites the declaration/kind columns. `scalar=true` uses
the stack oracle; `false` uses the batch sort/scan and bounded fallback.
Neither reads the producer's declaration answers. The statistics count event
identifiers and backward candidate walks; the source benchmark uses the
number of source tokens with nonzero `token_events` as its denominator, so
parameter reactivation and builtin declarations do not inflate throughput.

All columns have `count` logical entries. Element sizes are 4, 4, 1 and 1
bytes respectively, with their arena alignment; consumers must use bounded
loads and cannot assume padding. The output arena owns the stream and columns
until it is reset. It must be distinct from `parser.scratch`, and resolution
scratch must not alias the retained output. The parser's token/tree storage is
borrowed during build only. Empty units are valid. Counts and capacity
arithmetic are bounded before allocation. A failed build rolls back the
output/scratch marks and clears `ParseBindings`; a failed resolution restores
scratch and leaves the previous columns intact. Parser output with errors is
rejected, including recovered partial trees.

The trace has one active event per scalar undo-log entry, after the builtin
typedef prefix. Tag and label declarations reserve a name-zero undo entry,
which leaves ordinary-name meanings unchanged. Consequently the parser's
existing prototype, block, selection/iteration and substatement undo marks
restore all three namespaces at the same point. Prototype parameters, tags
and enumerators keep their declaration identities when reactivated in a
definition's body; they stay hidden in the rest of a return declarator. K&R
definitions save their final typed parameter declarations before retiring the
declaration-list scope. Record member lists are not C scopes.

One linear prepass collects ordinary labels for each function, allowing
forward `goto` and label-address uses. Dense local-label heads and block undo
marks exclude definitions covered by a GNU `__label__` declaration from that
function-wide list. Local label uses and definitions resolve to the
`__label__` declaration's identifier. No tree traversal recurses.

The current source adapter replays the scalar grammar once after the syntax
parse. It is explicitly opt-in, and its full production cost is measured. It
does not claim that batch resolution avoids that ordered grammar work, nor
that kernel speed alone establishes compiler throughput. Fusing the recorder
with a requesting consumer's scalar parse is a future integration choice.

## Scoped typedef classification

K3's vector keyword scan also records matching closers. Its sparse typedef
records carry the name, original declarator, complete-declarator activation
point, enclosing block's exclusive end and previous same-name record.
Classification replays those activations into a dense head array while
visiting identifier masks, and pops expired records lazily. Both the vector
classifier and its scalar twin start from the same immutable records. A
typedef's own declarator token is classified as an identifier being declared.

This keeps ordinary block-scope aliases, nested/sibling aliases and chains on
the vector path without a complete scalar preparse. An ordinary declaration
that hides a typedef still flags its containing item for exact scalar
handling. Unsupported typedef grammar also retains the existing fallback.
The fallback reconstructs file-scope meanings from each record's scope,
preserving an outer alias when an inner block reuses its spelling.

## Source tests and corpus qualification

`./build/compiler test-parser-bindings [--seed N] [--count N]
[--clang [PATH]] [--keep] [preprocessor options] [file or directory...]`
compares the direct producer, independent stack replay and batch declaration
columns for every relevant identifier, using both parser schedules. With no
files it checks hand-written identity/kind expectations, generates valid
nested scope programs with a separate expected-binding stack, verifies
failure rollback and metadata, and requires simple scoped typedefs to stay
off the parser fallback. Fixtures cover complete-declarator timing,
enumerators, independent tag/ordinary namespaces, tag redeclarations,
prototype/body visibility, function-style and K&R parameters, unbraced
selection/iteration scopes, forward/local labels, and member separation.
Clang is the external source acceptance oracle, not the binding oracle.

Cloud correctness checks on 2026-10-23 used Clang 23.1.1
(`6dfe1677ab8dffbc6ec13d53a1e0215d75147689`) and an AVX-512 EPYC VM.
The focused executable used `-O1 -march=native -fwrapv -funsigned-char
-fno-strict-aliasing -Wall -Werror -Wno-unused-function`. These are correctness
runs, not Zen 5 performance evidence. After integrating main's #296/#301
parser and radix changes, the final fixture/generator run passed 975,195
checks with no failures under AddressSanitizer and UndefinedBehaviorSanitizer.
The three external corpus runs below also passed under both sanitizers on
that merged tree: 58,909,221 checks across 954 files, with no rejected input
or declaration-column mismatch.

| External source | Input identity | Files | Source identifiers | Candidate walks |
| --- | --- | ---: | ---: | ---: |
| SQLite 3.53.4 amalgamation | `sqlite3.c` SHA-256 `b1dd5d74ec7f29055a6684fa06fb3c2f6821c87dd38f9a458dfd2e8a1db28189` | 1 | 141,547 | 0 |
| Zig 0.14.1 `lib/libc` | Archive SHA-256 `f4e576ee050073f9cc42a2978994e07522701606678930b187e4d5e1334bfaf3` | 733 | 632,195 | 0 |
| c-testsuite | Commit `5c7275656d751de0e68b2d340a95b5681858ed07`, `tests/single-exec`, GNU17 | 220 | 42,114 | 10 |

Each row passed with both parser schedules and all three resolution paths.
Counts above describe one parser schedule; the test summary counts both.
The libc selection comprises the `.c` files accepted by Clang with
`-std=gnu17 -fsyntax-only -ferror-limit=1 -w` and the same system headers.
The cloud required `-isystem /usr/include/x86_64-linux-gnu`; repository-source
checks additionally use the native predefines and explicit Clang 23 resource
headers with `-nobuiltininc`. `main.c` also passed, including source-event
fallback after many same-name declarations. Its changing token/identifier
counts are reported with each run rather than treated as a frozen corpus.
External inputs are read at test time and are not vendored.

## Source benchmark

`./build/compiler benchmark-parser-bindings [--passes N]
[--only prepare|scalar|batch|sort|source-scalar|source-batch]
[--after-scalar MS] [preprocessor options] file...` reports median
nanoseconds and available hardware cycles per source identifier, unit time,
arena bytes, sorting share and candidate walks. Every timed result is checked
against the original declaration column. `prepare` includes trace storage,
label preparation and the scalar grammar replay. `scalar` includes reusable
stack replay and output-column mapping; `batch` includes key construction,
sorting, scope resolution and mapping; `sort` includes key construction.
`source-*` includes syntax parsing, event production and the chosen resolver.
Both source modes use the default vector syntax parser; scalar/batch selects
the resolver only. Preprocessing is outside all these measurements.

The protected 9700X workflow runs the source fixtures and main/SQLite
comparisons, then measures `main.c` and the existing frozen SQLite corpus.
On pushes to main, dispatched runs and pull requests that change the parser
or the binding modules, it also isolates SQLite's scalar and batch paths and
runs the batch after 200 ms of scalar work. The runner acquires the repository's shared host lock;
manual runs must acquire it separately and pin to CPU 2. A batch path that
loses to stacks is retained as the tested alternative, not advertised as a
speedup.

### Ryzen 7 9700X qualification, October 23, 2026

[Workflow run 39933524737](https://github.com/buster14a/buster/actions/runs/39933524737)
passed the full production build, all 48 parallel test commands and all
executed benchmark stages with Clang 23.1.1. It checked out GitHub's test merge
`3f8fcd5e14b0683cbe5c7cebb19346645c201943`, whose tree
`a379d706a27a9b6b8fdf3bb66ddca05e3d2285f7` is identical to PR #320's source
head `d7cdc07f835173e89eb5b9c878e0525046b4b977`. The runner's job hook held
the shared host lock; each benchmark used `taskset` on its first permitted
CPU. These are 21-pass medians, with preprocessing excluded. Time and cycle
medians are selected independently.

The main input has 2,295,937 tokens and 466,590 source identifier tokens.
SQLite is the workflow's frozen `preprocessor-391e729af658` corpus, verified
against its pinned manifests and headers; it has 705,670 tokens and 143,019
source identifier tokens. This differs from the cloud SQLite 3.53.4 input
above. The [captured commands and output](https://github.com/buster14a/buster-performance/blob/a00d16e47b6f1090b8a0424ab0dd8cb4e6016713/docs/measurements/parser-bindings-128-2026-10-23/9700x-source-bindings.log)
preserve the exact flags and timings.

| Path | main.c cycles/identifier | SQLite cycles/identifier | main.c ms/unit | SQLite ms/unit |
| --- | ---: | ---: | ---: | ---: |
| Event production (`prepare`) | 492.701 | 521.836 | 41.649 | 13.528 |
| Reusable stacks plus column | 40.109 | 48.302 | 3.377 | 1.246 |
| Batch plus column | 98.779 | 94.318 | 8.315 | 2.446 |
| Key construction plus sort | 42.648 | 30.367 | 3.590 | 0.783 |
| Syntax, production and stacks | 774.782 | 793.126 | 73.621 | 24.286 |
| Syntax, production and batch | 832.172 | 847.128 | 78.418 | 25.701 |

Sort/packing accounts for 43.17% of main's batch-and-column time and 32.01%
of SQLite's. Main inspects 1,854 previous candidates across 434 uses, with
187 uses reaching the bounded scalar fallback; SQLite has zero walks or
fallback uses. Every measured declaration column matches the direct producer.

Separate SQLite processes measured stacks at 45.468 cycles/identifier
(1.172 ms/unit), batch at 85.646 (2.211 ms/unit), and batch following 200 ms
of scalar work at 84.677 (2.187 ms/unit). Thus reusable stacks win on both
measured sources, including the isolated SQLite comparison. The batch remains
an exact tested alternative. Its kernel timing does not justify replacing
the stacks on these inputs, and the larger event-production cost supports
keeping the source adapter opt-in until a consumer can fuse the recorder
with its parse.

Reported permanent output capacities are 299,205,750 bytes for main and
92,026,236 bytes for SQLite; reusable replay/result capacities add
129,305,958 and 39,806,256 bytes respectively. These conservative capacities
include spare slots and omit parser/trace scratch and peak RSS. They are not
measurements of the minimum binding representation.

The same target run passed 975,195 source fixture/generator checks,
27,551,260 main-source checks and 8,468,056 frozen-SQLite checks. Its call
graph gate covered 32 translation units, 16,624 functions and 90,761 calls,
with zero unapproved cycles. Scalar/vector parser and binding outputs agree.

The implementation, tests, generator and timing/counter code are original
project code. No external implementation, generated table or corpus is
incorporated. Existing arena and intrinsic interfaces are used directly.

## Boundary audit #298

Audit pin: `42630114aeb0553c3e2fabfd7fe9bc84b0ce6126`;
[scope and evidence](simd-boundary-audit.md). The
[event producer/solver contract](https://github.com/buster14a/buster/blob/42630114aeb0553c3e2fabfd7fe9bc84b0ce6126/src/buster/compiler/c_bindings.c#L4)
and [actual checker consumer](https://github.com/buster14a/buster/blob/42630114aeb0553c3e2fabfd7fe9bc84b0ce6126/src/buster/compiler/c_check_binding.c#L4)
are implemented. `check_input_resolve` emits declare/use/restore events while
elaborating declarations; `check_input_bindings` invokes `c_bindings_batch` or
its independent scalar replay and maps activation handles to original token
identities/types for expression reading. Parser source-column extraction in
PR [#320](https://github.com/buster14a/buster/pull/320) is a separate pending producer.

The event SoA has u32 key/declaration/point/end/previous/depth columns and byte
operation/kind columns, with explicit count/capacity and dense namespace heads.
A result is activation event + 1 (zero unresolved), not a declaration token;
`declaration[result - 1]` supplies that token. Declaration activation follows
the complete declarator, before its initializer; repeated parameter activation
retains original source identity. Tags and labels have separate namespaces;
member lookup belongs to the type table. Restore closes visibility intervals.

Caller arenas own the events/results; workspace is private to a compilation
thread. Internal allocations are 64-byte aligned, but input padding is not
required. Capacity failure is sticky; a caller rejects the unit and resets.
Checker scratch results are released only after mapping them into retained
columns. An empty stream allocates nothing there. On resolver/allocation
failure it publishes no usable use results, rather than accepting partial IDs.

The batch solver stably sorts by namespace/name/event position, then scans
16 candidates and bounds backward visibility walks before one lazy scalar
replay. Sorting does not reorder declarations in the language. A full event
stream or an explicitly retained initial environment is needed: arbitrary
independent event slices lose earlier declarations/restores. Reset reuses heads
across units without clearing every process-wide name.

The current checker constructs its own binding events. After #320 it also
replayed the parser's grammar for the adapter's columns and mapped them onto its
uses, a duplicate syntactic producer.
[#336](https://github.com/buster14a/buster/issues/336) measured the whole reader
with each handoff and made the reader's retained answers (each use's `previous`)
the column, with the stack replay as `--scalar`'s twin; the parser's replay and
the batch sort cost more on every measured input
([the checker's binding handoff](c-check-bindings.md)). It preserves [#128](https://github.com/buster14a/buster/issues/128)/[#133](https://github.com/buster14a/buster/issues/133)/[#113](https://github.com/buster14a/buster/issues/113)
owners; synthetic batch timings are not a source acceptance result.
Disposition: retain the integrated checker path at the pin. `test-bindings`
and checker source tests cover scope order, guard tails, capacities, independent
replay and source identities. [#128](https://github.com/buster14a/buster/issues/128) and [#336](https://github.com/buster14a/buster/issues/336) retain actual corpus, whole-path
memory, sorting share, event/identifier denominators and fallback acceptance.
