# Foundation components

[Catalogue](../../PROJECTS.md) · Area: `foundation` · [Tracking](../project-tracking.md)

## Purpose and consumers

Provide shared C primitives for Buster's applications and libraries. The compiler
and runtime are existing consumers, but a primitive's contract belongs here rather
than being defined as a compiler feature. These modules share repository build
conventions; this page does not claim a separately installed library or stable ABI.

## Capability entry points

| Stable feature ID | Existing API/source | Scope and boundary |
|---|---|---|
| `foundation.arenas` | [arena.h](../../src/buster/lib/arena.h) | Arena allocation and lifetime management. Platform commitment/prefault behavior is described in the [platform contract](../agents/platform.md). |
| `foundation.strings` | [string.h](../../src/buster/lib/string.h) | The `String8`/`S8` vocabulary and shared string operations. Use each function's actual bounds, encoding and ownership contract. |
| `foundation.bytes` | [byte_writer.h](../../src/buster/lib/byte_writer.h), [hash.h](../../src/buster/lib/hash.h) | Byte construction and hashing helpers; do not infer cryptographic guarantees from a generic hash API. |
| `foundation.simd` | [simd.h](../../src/buster/lib/simd.h) | Shared SIMD vocabulary with feature/compiler guards and fallbacks under the [SIMD guide](../agents/simd.md). |

Numeric, time and base definitions remain alongside these modules in
[src/buster/lib](../../src/buster/lib/); the table is an entry-point index, not
an inventory of every helper or a blanket support claim.

The string formatting family consumes typed placeholders such as `{S8}`
and `{u32}`; its complete vocabulary and integer modifier syntax are documented
at `string_format` in `string.h`. Escape a literal opening brace as `{{` and
a closing brace as `}}` (a single closing brace also remains literal).
Brace-bearing C source can be passed unchanged as a `{S8}` argument.
Malformed and unknown placeholders are programming errors: they fail with a
fixed raw diagnostic naming the cause and opening-brace escape, without
re-entering formatting or allocating while reporting the failure.

## Validation and work

Use the affected existing module tests and [test registration rules](../agents/testing.md).
For memory ownership changes include relevant consumers; for SIMD preserve exact
lane/mask semantics and supported fallbacks. Keep performance claims separate from
functional correctness, and record the actual tested revision on the PR.

[#129](https://github.com/buster14a/buster/issues/129) is an existing consumer-led
SIMD vocabulary work item. It is a shared-component task with compiler consumers,
not a reason to duplicate the same primitive issue under every compiler phase.
Do not treat the age of an issue as proof that its original missing operation is
still missing; inspect current source before implementing it.
