# Canonical and machine validation boundaries

The block-row family (instruction ownership, termination, result binding
and reference range) is established at construction by the protocol in
[canonical IR construction](canonical-ir-construction.md); the boundaries
below remain its independent checks.

This contract complements the [frontend guide](agents/frontend/foundations.md),
[machine ownership inventory](machine-metadata-ownership.md), and
[differential testing](differential-testing.md). The focused implementation is
[GitHub #294](https://github.com/buster14a/buster/issues/294), not a replacement
for machine metadata work (#45). Dense canonical finalization is described in
[canonical CFG publication](canonical-cfg-publication.md).

## Integer constant rows

A `CONSTANT_INTEGER` row spells a signed number as `immediates[0]` plus
`immediate_is_negative`; its value is that number reduced modulo 2^width
(`ir_integer_constant_decode`). The validator requires the spelled number to
lie in `[-2^(width-1), 2^width)` (`ir_integer_constant_canonical`), so a
reader that materializes the magnitude unreduced -- the native emitters do --
sees the same bits as one that reduces it. The C producer's single row
emitter (`c_ir_emit_integer_value_at`) reduces an out-of-range spelling such as
a bit-field clear mask `~mask` built at 64 bits for an 8-bit access.

## SWITCH case images

Canonical SWITCH equality compares the selector and each raw case key modulo
the selector's declared integer width, independently of signedness. For an
8-bit selector, keys 7 and 263 therefore name the same bit pattern; both a
low-width all-ones key and `UINT64_MAX` match signed -1. A singleton key may
retain high bits in its raw payload. Case keys must be distinct under this
same equality; defensive uniqueness validation is tracked separately in
[#2378](https://github.com/buster14a/buster/issues/2378). The final target remains
the default, and different cases may share a destination.

The native MIR selectors and direct emitters form matching selector/key images
at the existing SWITCH boundary. Their 32/64-bit machine comparisons clear any
extension beyond a narrower semantic width; 32-bit selectors compare at
32 bits, and 64-bit selectors preserve the full image. Source keys and targets
remain unchanged. LLVM's typed case constants, eBPF's image normalization and
the direct Wasm emitter use the same equality. Wasm compares zero-extended
selector-width images in its existing i32/i64 carrier, masking raw keys at
the SWITCH boundary independently of signedness
([#2385](https://github.com/buster14a/buster/issues/2385)).

Registered `codegen_test_canonical_switch_key_images` redirects a lowered C
SWITCH to its original typed ARGUMENT, bypassing C's integer promotion. It
validates raw IR before each native consumer and requires successful emission
with zero fallback in every allocator. Signed/unsigned 8/16/32/64-bit inputs
cover singleton aliases, low/full-width all-ones keys, positive/default controls
and a distinct high 64-bit key. Unsanitized desktop runs call the emitted
function through a matching host C signature and check integer bit-vector
expectations; sanitizer/mobile runs retain validation and emission controls.

### Direct Wasm SWITCH execution

Registered `compiler_driver_test_wasm_switch_images` commits typed ARGUMENT and
SWITCH rows directly, preserving narrow types without C integer promotions.
Signed/unsigned 8/16/32/64-bit functions cover raw singleton aliases,
low/full-width all-ones keys, multiple distinct cases, shared destinations and
the final default. The 64-bit control distinguishes 7 from `0x100000007`.
Canonical preparation validates the module before emission; snapshots require
the source selector, raw keys and target order to remain unchanged.

Each Wasm32/Wasm64 module is emitted twice and must be byte-identical. An inline
Node oracle uses independent BigInt bit-vector equality, thirteen literal
expectations, exhaustive 8-bit inputs, wider boundaries, signed images and dirty
carrier bits. Each pointer-width run requires 8,845 export calls. The existing
bounded Node runner requires a normal zero exit, empty stderr and the exact
terminal summary. SHA-256 and before/after file comparisons bind execution to
the compiler's original bytes. Missing Node reports an execution skip.

## Unary value categories

Every `UNARY` operand and result is a `VALUE` at the operation's canonical
type. A storage `PLACE` requires an explicit `LOAD` before integer, floating,
Boolean or vector unary arithmetic; matching type IDs do not authorize an
implicit load or an addressable unary result. Invalid categories are refused
with `IR_VALIDATION_OPERATION` at the unary row.

`ir_test_canonical_unary_categories` uses an original complete raw
`LOCAL` -> `LOAD` -> `UNARY` -> void `RETURN` fixture for all ten operations.
It preserves each valid family while independently replacing the operand,
result or both with places, and includes invalid operand/result categories.
Uncertified preparation must reject these controls at `CANONICAL_INPUT`,
before CFG or completion publication and without mutating the source rows.
Valid neighboring modules prepare, publish zero edges and revalidate.

## Scalar binary operation families

Scalar arithmetic and numeric comparisons require the operation's family to
match the operands: integer operations take integer values, and floating
operations take floating values. Arithmetic preserves that canonical type;
comparisons produce Boolean results. Matching operand and result type IDs
alone does not make a cross-family operation valid. Boolean, pointer and
vector operations retain their separate rules.

`ir_test_canonical_binary_families` calls the complete canonical validator for
all 33 scalar arithmetic/comparison operations with matching and wrong-family
operands. It pins `IR_VALIDATION_OPERATION` at the binary row for every
wrong-family case and preserves Boolean, pointer and vector controls.

## Label provenance validation work

Global label-address relocations resolve their function owner through a lazy,
module-local scratch index. The first lookup indexes each function once and
subsequent lookups use a half-full hash table. Duplicate symbol ownership keeps
the first function in module order, including its lowered-state check. Modules
without label relocations allocate no owner index. Symbol kind/definition,
block bounds, addends, initializer extents and relocation overlap remain
independent rejection conditions (#2444).

Label sets and provenance paths retain their caller-supplied order. Validation
borrows ordered arrays and constructs immutable radix-sorted scratch views for
larger unordered arrays; sets of at most eight IDs use bounded small-set work.
Uniqueness checks adjacent IDs, set relations merge sorted views, and shape
validation uses indexed membership plus coverage marks sized to the aggregate
set rather than the function's entire block universe. The complete value check
reuses its shape proof instead of recomputing uniqueness (#2445).
Each path's temporary block sort rewinds after updating coverage, so resident
scratch is bounded by the aggregate set, the largest path set and the path-order
view even when many paths share one unordered block array. The cumulative
requested-byte counter still counts those repeated temporary copies.

Path shape validation checks adjacent intervals in offset order after proving
their extents cannot overflow. Exact subrange transfer walks sorted source and
result views together, translating offsets and preserving size, label sets and
non-label flags. Missing and extra result paths both fail; touching intervals
remain disjoint (#2446). These views live only for their validation call and
never certify future mutations or replace the strict canonical-input boundary.

Transfer and standalone parameter-provenance checks first require backing for
their nonempty block/path arrays, including nested path block sets and every
aggregate/incoming operand. Malformed metadata therefore refuses before a
provenance query can traverse it; this is a backing check rather than another
complete shape or uniqueness proof (#2480).

The optional allocation-diagnostic counters report actual element visits,
membership/hash probes and requested scratch bytes for these mechanisms.
Fixed radix-bucket setup, memory copies and allocator overhead are not counted
as element comparisons. Geometric registered regressions constrain the work
and compare independent malformed-input expectations. Whole-compiler timing
and hosted noise are reported separately; a better scaling bound is not an
end-to-end throughput measurement.

## A certificate describes one input

`CIRLowerResult.canonical_ir_certified` describes the successful C producer's
output. The `input_certified` argument of `ir_prepare_canonical_module` permits
skipping its **input** scan. Promotion consumes that input proof: it rewrites
instruction/value IDs, block parameters, incoming values and opcode summaries.
The input certificate is not evidence about those rewritten rows.

After actual promotion (`promoted_locals != 0`), preparation invokes the existing
`ir_validate_canonical_module` when the input was uncertified or when any of
these compile-time conditions holds:

- `!BUSTER_OPTIMIZE` (debug/unoptimized build), `BUSTER_INCLUDE_TESTS`, or
  `BUSTER_SANITIZE`;
- the explicitly enabled `BUSTER_VERIFY_IR_TRANSFORMS=1` diagnostic/measurement
  switch.

Setting the explicit switch to zero cannot disable debug, test or sanitizer
checks. An optimized, non-test, non-sanitized production build retains the
existing pass-contract fast path; it trusts the transformation's implementation,
not an assertion that the producer validated its output. Disabled, unchanged
and already prepared certified modules do not gain a redundant output scan.
The FAST input guard consumes the same fact: when the promotion-output scan
or the uncertified input scan has passed over the module and nothing has
mutated it since, FAST starts from that scan rather than repeating it.
The existing `-fverify-codegen` option still forces independent input validation
and native MIR/placement checks, including in optimized production builds.

`local_promotion_complete` records successful pass completion, **not** a general
IR certificate. Failed output validation does not publish that marker. A caller
that later mutates prepared IR must call `ir_function_invalidate_cfg` before
editing, reacquire any builder-node pointers, discard its certificate and supply
`input_certified=false`. Preparation validates the current representation even
when the pass-completion marker is already set. A failed result must not be
consumed by code generation or retried with the stale input certificate.

Every canonical consumer also calls preparation itself, so a direct caller that
never prepared its module still gets a validated one. When the driver has just
prepared the same unchanged module, that call would be a second whole-module
scan, not a new boundary. The driver therefore hands its preparation to every
consumer it selects: native code generation through
`CodegenModuleOptions.assume_validated`, LLVM bitcode through
`LlvmBitcodeOptions.validate_ir = false`, and the Wasm and eBPF emitters
through `WasmOptions.assume_validated` and `EbpfOptions.assume_validated`.
Each of these makes the consumer's own preparation certified, which is a no-op
on a prepared module. A zero-initialized Wasm or eBPF options structure keeps
the validating default for direct callers; `-fverify-codegen`, which makes the
driver prepare uncertified, is refused for these non-native targets.

`IrValidationResult.boundary` identifies the last preparation scan, on both
success and failure: `CANONICAL_INPUT` or `LOCAL_PROMOTION_OUTPUT`. Raw verifier
calls and certified preparation that performs no scan return `UNSPECIFIED`.
The field is diagnostic context, not a persistent proof or mutation counter.
The driver reports it alongside the existing error, function, block,
instruction and opcode context. Failed finalization reports `CFG_PUBLICATION`;
successful publication preserves the preceding scan's boundary. Publication
checks topology/ownership and explicitly remaps instruction identities, source
rows and sparse extras, without granting a semantic certificate to future
mutations. The cold result grows; authoritative canonical
instruction/value rows and machine rows do not change size.

## Existing checks and limits of the evidence

Parameter provenance validation selects its route once per function. When
`label_metadata_count == 0`, every provenance lookup is the zero record, so
validated incoming IDs/types establish the provenance predicate without another
walk. Mutable parameters still require the exact count, predecessor order,
incoming IDs/types, list exhaustion and `last_incoming` identity in the first
walk, including a null tail for zero incoming values. Published parameters retain
the independent CFG extent/topology proof and their incoming ID/type walk.
Functions with any metadata retain the existing provenance calculation; the
exported standalone mutable helper retains all of its checks and scratch work.
`VALIDATION_PARAMETER_PROVENANCE_CHECKS` counts actual provenance calculations.
The registered parameter fixture covers both representations, malformed
structure, poisoned scratch, zero incoming values, empty metadata records and
an actual label-set union with a deliberately incomplete destination. Removing
the metadata-free calculation is a bounded work reduction, not a measured
whole-compiler throughput or RSS result.

| Boundary / owner | Existing checks reused | What a successful check does not establish |
| --- | --- | --- |
| Canonical input and promotion output / `ir_validate_canonical_module` | Required storage; instruction-chain ownership; block sealing and termination; one definition per value (a row or one block parameter, never both); value and operation types; call/return signatures; parameter/incoming types, counts and predecessor order; branch-target validity; global alignment, initializer and relocation ownership | This change does not add a whole-function canonical dominance proof or prove full CFG predecessor/successor symmetry. Those properties must not be inferred merely from valid IDs and parameter counts. |
| Dense CFG publication / `ir_function_publish_cfg` and published-shape validation | Bounded instruction ownership; every tail a terminator and no terminator followed by a row (`ir_instruction_is_terminator`), for every producer including certified input; terminator-derived unique edges, including parameter-free edges; exact optional builder predecessor lists and incoming extents; dense parameter/argument slices; remapped instruction sources, value definitions and sparse extras | These structural checks do not replace canonical type/operation/provenance validation or grant a semantic certificate. They do not prove whole-function canonical dominance. |
| Selected MIR / `machine_verify_function` | Side-table bounds; opcode and operand kinds; register classes and physical limits; fixed/tied constraints; block instruction coverage and terminators; parameter/edge-copy classes; definition counts/points and immutable-register dominance, including edge uses | Opcode-specific payload validation is not an independent proof of every emitted instruction's width/encoding. Legacy explicitly mutable registers retain their separate definition contract. Bounded edge spans alone are not a proof of complete CFG symmetry. |
| Scheduled MIR / native code-generation verification path | With `verify_invariants`, reruns the same machine verifier on the reordered candidate, before replacing the accepted function or using its rebuilt placement | The selector's original certificate cannot certify reordered rows, remapped definition points or the new placement. This slice leaves the existing opt-in scheduled-MIR hook and its counters unchanged. |
| Placement / native code generation | Existing valid-placement checks and strict `verify_invariants` failure handling | Structural IR verification does not prove ABI behavior or generated-program semantics. Use the existing independent compiler/runtime matrix. |
| Relocations / canonical module and object emission | Canonical globals validate label ownership and relocation storage/ranges; MIR validates its local symbol-reference metadata | A standalone machine-function verifier lacks module symbol/definition ownership. Preserve module/object checks rather than treating a local MIR success as relocation certification. |

There is no new permanent use-list representation. Canonical compaction and
machine scheduling must rebuild or remap their derived use/liveness/placement
facts; the ownership inventory records their producer, lifetime and invalidation.
This inventory is source-level reconciliation, not a claim that all remaining
CFG, dominance, width and relocation hypotheses were reproduced or repaired.

## Switch case keys

SWITCH case keys must be distinct after reduction to the selector's integer
width. Signed raw encodings keep their low-width bit pattern. Wider-than-64-bit
selectors use the existing zero-extended 64-bit case payload. Repeated
destinations are permitted; case order and the default-last target contract
are preserved. This prevents duplicate constants in the LLVM switch consumer.
It does not assert identical narrow-selector execution across native consumers,
whose existing comparisons use 32/64-bit widths.

Validation scans strictly ascending/descending normalized keys without scratch
allocation. Unordered keys use a scratch copy and at most eight radix passes;
validation never sorts caller-owned immediates or targets. The registered
`ir_test_canonical_switch_keys` uses raw original ARGUMENT/SWITCH/RETURN rows,
explicit valid/invalid keys and 4096-case permutations. It checks width aliases,
signed/full-width endpoints, shared destinations, exact error context and
uncertified preparation refusal before publication, with unchanged input arrays.

## Regression and measurement contract

`ir_promotion_tests` now runs the existing structural corpus with both certified
and uncertified input. It asserts disabled/unchanged/idempotent behavior and the
actual output-check boundary whenever promotion removes rows.
`ir_promotion_validation_tests` deliberately introduces safely backed faults
outside the function that is promoted: global alignment, missing relocation
storage, a data symbol misused as a label owner, an unterminated block, and a
return-type mismatch. Uncertified input must fail before mutation; a deliberately
stale certificate must allow real promotion but then fail at the output check,
with exact contextual IDs and no completion publication. A separate case
revokes the certificate after a later mutation of already prepared IR.
These are negative hook controls, not frontend or promotion miscompilation
reproducers. They use the existing registered IR module, not a parallel harness.

Integration against main also exposed previously certified invalid frontend
output: complex predicates used integer bitwise operations on Boolean values,
and qualified field initializers could capture an unqualified value at a
different field type. `basic_c_ir_validation_values.c` exercises the repairs
with strict native verification/execution in both frontend forms and all four
allocators. The registered IR tests validate the raw producer output across
x86-64, AArch64 and Wasm64 layouts, retain volatile accesses, then prepare it.
Negative Boolean-operation controls reject integer opcodes, non-Boolean results
and place results. The existing aggregate operand/type checks remain intact.

For a negative control, restore only the old post-promotion condition
`!input_certified` in an isolated CI workspace while retaining the new tests and
context. The certified-output assertions must fail. This control is not an
acceptable production configuration and must not replace any required gate.

Measure optimized production builds of the **same source** with tests and
sanitizers disabled, changing only `BUSTER_VERIFY_IR_TRANSFORMS` between zero
and one. Reuse `bench_throughput run`, frozen generated inputs, alternating pairs,
all four allocator modes, and `--require-identical-output`. Include
`--flag -fno-frontend-ssa` so the canonical-promotion workload is not accidentally
measuring an unchanged direct-SSA module. Record binary/input/output hashes,
compiler build flags, host identity, raw samples, CPU/wall time and available
RSS/counters. The ordinary base-versus-head throughput CI gate remains separate.
Build time, compiler implementation throughput and generated-program runtime
are different measurements. This contract does not supply an unrun measurement.
