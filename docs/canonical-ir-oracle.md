# Bounded canonical IR executable oracle

The test-only evaluator in
`src/buster/tests/compiler/ir/ir_oracle_internal.h` reads actual canonical IR
records. Its integer rules, byte memory, call stack and dominance check do not
call `ir_integer_*`, folding, lowering, instruction selection or ABI routines.
Canonical validation remains a separate prerequisite. Native emission and
object linking consume the same IR contract independently.

After an ordinary Release build, run from the repository root:

```sh
build/Release/ide test --ci=1 --verbose=1 --module=ir_oracle_tests
```

This command interprets the reference fixtures, emits their native counterparts
in private child processes, and prints `IR_ORACLE_REPORT_V1` records. Reproduce
with the exact source revision and the six fixed input pairs in
`ir_oracle_inputs`. The native execution slice is unsanitized Linux x86-64;
other builds run portable evaluator/rejection/report controls and explicitly
print `native-unavailable`. That status is not native semantic agreement.
No hardware performance acceptance follows from these correctness checks.

## Admitted semantics

| Boundary | Declaration |
| --- | --- |
| Target | Little-endian, 64-bit pointers; independently checked natural scalar layouts. |
| Integers | Boolean (one bit, one byte), 8/16/32/64-bit integers. Arithmetic wraps modulo the declared width; signed comparisons/division/right shifts use explicit two's-complement rules. Sign/zero extension, truncation and integer reinterpretation are supported. |
| Invalid arithmetic | Division/remainder by zero, signed MIN/-1, shift counts at least the width, and zero CLZ/CTZ refuse evaluation. |
| Control flow | Declared entry, branch, Boolean branch, integer switch and simultaneous block-parameter assignment; builder and published CFG representations. UNREACHABLE is admitted as a terminator but execution is invalid. Independent operand and incoming-edge dominance checks prevent stale loop values. |
| Storage | Scalar LOCAL creates one fresh allocation identity per activation; executing that LOCAL again is unsupported. Locals start uninitialized. Scalar defined globals admit zero/integer initialization without relocations or TLS. |
| Pointers | Tagged allocation identity and byte offset, never a host address. Address-of/dereference and nonnegative scalar pointer indexing; one-past formation may occur, dereference may not. Bounds, initialized bytes, natural alignment, read-only globals and activation lifetime are checked. Allocation IDs are never recycled within a run. |
| Calls | Fixed-prototype, same-module ordinary functions with integer/Boolean parameters and integer/Boolean/void results. Explicit frames, no input-dependent host recursion or host resolver. |
| Observables | Complete return bits and the declared initialized scalar global `sink`; a generated native `observe` function reads the counterpart's sink. No addresses, padding or uninitialized data are compared. |
| Limits | At most 64 blocks, 1,024 instruction rows and 256 values per function; 8 arguments, 16 active frames, 64 allocation identities, 4,096 bytes and 4,096 executed rows per run. Exhaustion is a distinct refused result. |

Pointer-valued memory, pointer/integer casts, pointer arguments/returns,
negative pointer indexing, aggregates, vectors, floating point, atomics,
volatile accesses, dynamic stack, varargs, assembly, undefined values, external
calls and nontrivial global initializers are outside this slice. Unsupported
opcodes are refused even when their rows lie on an untaken path. A reached
unsupported type/operation is also refused. This is a fixture oracle, not a
portable production interpreter or a proof of the entire module's semantics.

## Independent witnesses and controls

Four fixtures build canonical types, symbols and rows without the C frontend.
They exercise widths, wrapping arithmetic, branching, initialized local
loads/stores, address-of/dereference, a same-module call and an observed global
store. Two separately lowered C fixtures exercise calls and a loop carrying
swapped block parameters. Literal mathematical expectations accompany both
routes. Interpreting incorrectly lowered C IR cannot alone validate lowering.

The native child uses the existing canonical code generator and object linker;
it has no external symbol resolver. Each child has its own process group,
30-second deadline and bounded stdout/stderr capture. A deliberately looping
fixture separately tests the 4,096-row interpreter budget and a five-second
native-child timeout; neither result can become agreement. Requested allocator
modes report the actual fallback-function count rather than implying that
every function retained machine emission. Agreement requires normal
zero exit, empty stderr, complete capture and cleanup, plus exactly six ordered,
typed observation rows. Canonical decimal parsing rejects overflow, duplicate
records, missing rows and malformed numbers. Output or a summary cannot replace
process completion.

Three deliberately wrong, separately rebuilt native fixtures replace addition
with subtraction, invert a branch, or store an argument instead of the correct
global result. The oracle interprets the unchanged reference. A successful
negative control requires a clean native run with complete observations that
disagree; invalid, unsupported, timed-out or incomplete results are inconclusive.
These fixture mutations are sensitivity evidence, not surviving production
compiler mutants or proof of compiler correctness.

## Existing implementations and next boundary

The shared compile-time integer engine is useful production infrastructure but
cannot supply the independent arithmetic core it is being challenged against.
The historical memory/effects oracle from #1981 evaluates an invented row
vocabulary and concretely initializes its local bytes; treating it as a
canonical IR interpreter was rejected. Backend-specific eBPF/Wasm oracles and
structural validators retain their separate roles.

The next useful extension is a small independently specified pointer-argument
and bounded-array fixture, with native byte observations. Add shadow pointer
metadata before admitting pointer-valued stores. General repeated-LOCAL
lifetimes and target-specific aggregate ABI behavior remain uncertain and are
explicitly excluded.

All fixtures and implementation are first-party work. Baseline inspection at
`6fc08ec475694aeb3b1062c1d390483a69668508` found no selected project-wide
first-party license in `THIRD_PARTY_NOTICES.md`. No external implementation,
test asset, dependency or vendored material was imported. Actual revisions,
executed gates, failures and integration ownership belong on #2229 / PR #2248.
