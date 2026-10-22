# Scoped C binding kernels

`c_bindings.c` implements the resolution kernels for
[#128](https://github.com/buster14a/buster/issues/128). This PR's bounded scope
is the reusable event-stream kernels, their oracle and tests, and their
synthetic benchmark. The optional parser adapter was authorized on
[#244](https://github.com/buster14a/buster/pull/244#issuecomment-6346496936)
and remains a separate implementation slice under #128. The compiler's normal
parse command does **not** yet produce resolved declaration columns from
these kernels, and this work does not close #128.

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

These are **event-stream** tests. They do not establish that the parser emits
correct events for C source. In particular, corpus agreement cannot be claimed
until the adapter exists. The 9700X workflow runs `test-bindings`.

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
[measurement record](measurements/simd-301-2026-10-23/README.md) preserves
raw process output, source and executable identities, commands, and limits.
9700X qualification remains separate.

## Remaining acceptance for #128

- Implement the authorized optional parser declaration/use/scope output, including
  ordinary versus member names, tag declarations versus references, parameters,
  enumerators, labels and recovery. The authorization and exact hook sites are
  [on #244](https://github.com/buster14a/buster/pull/244#issuecomment-6346496936).
- Integrate scoped typedef classification into K3 with its exact parser
  fallback, preserving the existing parser owner's changes.
- Check `(T) * x` and the scope fixtures through the actual parser; compare
  declaration columns for every relevant identifier in SQLite, libc and
  c-testsuite, with exact input provenance and walk counts.
- Measure both paths and sorting on SQLite and `main.c` on the locked 9700X.
  The synthetic benchmark does not establish a corpus winner or compiler
  throughput improvement.

The implementation, tests, generator and timing/counter code are original
project code. No external implementation, generated table or corpus is
incorporated. Existing arena and intrinsic interfaces are used directly.
