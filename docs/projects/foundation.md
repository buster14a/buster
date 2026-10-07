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

The shared unsigned bit-count helpers in [integer.h](../../src/buster/lib/integer.h)
return the operand width for zero: 32 for `u32`, 64 for `u64`. Their focused
`integer_bit_count_tests` fixture runs inside the registered `integer_tests`
module. It compares leading/trailing counts with division/remainder oracles,
exhausts all 16-bit values in low/high positions, and covers full-width bit
boundaries, complements and alternating patterns. Run
`build/Release/ide test --module=integer_tests --verbose=1 --ci=1` from the
repository root after building `ide`; this checks the implementation selected
by that build, including the scalar fallback on MSVC. Native platform and
self-host validation remain the existing CI gates.

`ArenaFlags.pool_reuse` admits custom reservation sizes only when the complete
intrusive pool link fits after the arena header. Smaller valid reservations
unmap on destruction or retirement. At the exact header-plus-pointer boundary,
pooled reuse retains the link's dirty watermark and zeroed allocation clears it.
The registered `arena_tests` cover every shorter payload length, both lifetime
endings, and an exact-fit two-entry pool whose non-null link needs clearing.

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

`string_first_sequence` finds the first exact byte sequence without modifying
either slice. An empty needle returns zero; an oversized or absent needle returns
`BUSTER_STRING_NO_MATCH`. It retains a direct scan below 32 bytes and uses unsigned
two-way critical-factorization search for longer needles, with linear combined
haystack/needle work. One initial equality probe avoids preprocessing if the full
needle already matches at offset zero; its at-most-needle-length work preserves
the linear bound. This is a shared primitive contract, not a compiler-throughput
claim. Null-empty slices, the threshold boundary, periodic and random bytes,
independent lengths and work-count negative controls are covered by
[`string_first_sequence_work.py`](../../tools/string_first_sequence_work.py).

The source-bound observer extracts the actual search and scalar equality bodies
into private generated C. Its counters measure logical byte comparisons,
including preprocessing and periodicity checks; they do not measure SIMD work,
retired instructions or elapsed time. The ordinary release path has no observer.
Run it with a fresh output directory, optionally `--cc gcc`; Workflow lint runs
the same bounded controls. For opt-in observations of the real uninstrumented
short-needle path, set `BUSTER_STRING_SEQUENCE_BENCH=1` when running
`build/Release/ide test --module=string_tests --ci=1`. The fourteen fixed cells
report checksums and timing for lengths 1, 2, 3, 7, 15, 31 and 32 with immediate
matches and late mismatches. Timing never gates correctness; matched serial
baseline/candidate builds and paired captures follow the
[benchmark methods](../agents/benchmarking.md).

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
