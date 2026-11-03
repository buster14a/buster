# Compact literal tables

`c_check_literal_table_input.c` implements the bounded file-scope slice of
#658. An eligible array initializer publishes one retained `CHECK_INIT_BYTES`
part rather than an expression node, initializer part and data-plan part for
each element. The parser's tokens still exist. This is not a compact parser
representation or a solution for all initializer forms.

## Admission and fallback

The existing integer path admits nonvolatile 8-, 16-, 32- and 64-bit integer
elements, excluding `_Bool`. The floating continuation admits nonvolatile
`float` and `double` elements. Both require at least 128 elements, a single
brace pair, and only number tokens, commas and optional unary minus signs.
A trailing comma is allowed. An explicit extent must fit every element;
omitted elements remain zero, and an incomplete array acquires its extent.

Floating literals are parsed by the ordinary literal reader, including their
source suffix's rounding. Same-format real literals can supply their bytes
directly. Other source types use the ordinary unary and initializer actions
and must convert exactly, without a diagnostic, warning or constant-evaluation
fault. The exactness comparison uses the host's 64-bit-significand `long double`,
not a prematurely rounded `double`, for ordinary integer literals up to 64 bits.
Integer `-0` becomes positive floating zero; floating `-0.0` retains its sign.

Precision-losing conversions, literal overflow or underflow warnings, imaginary
and bit-precise constants, malformed spellings, designators, nested braces and
general expressions keep the ordinary path for the **whole table**. Unsupported
element types, automatic objects, compound literals and volatile elements also
keep that path. In particular, this continuation does not add compact `long
double`, `_Float16` or floating-to-integer initialization.

The producer publishes no ignored token range or retained record until all
batches succeed. Failed candidates restore the input-arena suffix and spelling
scratch. Resource exhaustion remains a resource failure rather than being
misreported as successful fallback. Retained payloads, inferred types and
publication use the existing lifetime and deferred-replay contract.

## Data-parallel work

The existing grammar kernel checks 64 token kinds at a time. Each batch obtains
16 bounded spellings; common short integer spellings use the existing SIMD
decoder. Floating payloads are stored in masked eight-element batches, with
bit-preserving 64-to-32-bit narrowing for `float`. The scalar path stores the
same representations without vector instructions. Stable parser options and
token-kind pointers are cached across spelling normalization and output stores.

General floating spelling conversion deliberately reuses the scalar literal
reader instead of introducing a second decimal/hexadecimal rounding algorithm.
This removes per-element retained graph/planning state but is not a claim of
SIMD floating parsing or a measured throughput result. Native profiling must
establish the remaining parsing cost and whether another decoding kernel pays.

## Regression coverage and qualification

`test-checker` includes `c_check_literal_table_floating_test.c` through the
existing literal-table section. Its fixtures compare compact scalar and SIMD
paths with the ordinary per-element graph and byte planner. Coverage includes
127/128/129 and 255/256/257 elements, sized and inferred arrays, mixed suffixes,
negative zero, subnormals and finite extrema, zero tails, late precision-loss
fallback, volatile/designated/expression fallback, and deferred replay with
forced late fallback. Hand-written IEEE representations independently check
selected values. Every batch store tail, including an empty batch, ends at a
protected page. Allocation failures cover spelling scratch, payload and
retained-record reservations. Native source fixtures compare runtime reads
with Clang without casting extreme floating values outside `int`'s range.

These are added regressions, not a record that they have passed. The floating
continuation still requires hosted correctness, native scalar/SIMD correctness,
sanitizer qualification and locked 9700X A/B measurements before performance
acceptance. Keep new raw measurements and harnesses in
`buster14a/buster-performance`, not this repository. The older integer-table
measurements belong to their frozen revisions and do not qualify this change.
