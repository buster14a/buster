# Semantic action context and source-constant provenance

Issue #1058 separates three contracts: the source/evaluation context supplied to
an operation, the operation's operand-evaluation decisions, and the properties
of its resulting value. This is not a new evaluator or a per-node context arena.

## Context ownership and construction

`c_check_context.c` defines `CheckContext`. Its histories and const-object view
are borrowed, immutable for an action/batch, and must remain live until that
work finishes. The record copies pointers, not history arrays. No context is
retained in a static cache, and no context column is added to `CheckGraph` or
`CheckStream`. Type-table interning does not authorize mutation of the borrowed
histories. A producer must reacquire a receipt view after its owner changes;
copying a context never extends the owner's lifetime.

| Producer | Authoritative constructor | Deliberate policy |
| --- | --- | --- |
| Graph actions, graph regions, query proof, assembly and late atomic queries | `check_graph_context` | Uses the node's source end and token-origin system-header policy; its explicit permission argument does not replace evaluation masks. |
| Declaration-time expressions and builtin calls | `check_input_context` | Uses the declaration reader's histories and immutable receipt view; source constant evaluation has no runtime floating-environment dependency. |
| Static/runtime initializer conversions | `check_init_context` | Retains the initializer walker's source cut and receipt permission. A runtime initializer must not borrow permission from a graph that happens to expose receipts. |
| Direct action tests/API callers | Zero-initialized or explicitly populated `CheckContext` | Null completion history means the current type table; a non-null empty history makes no tag visible. Zero receipt permission means no receipt reads. |

Source positions are exclusive ends, independent of diagnostic node identities.
Completion and transparent-union activation histories share the bounded tag
index domain. `check_context_source_complete` handles nested aligned arrays;
`check_context_tag_visible` is the nominal-type query used by families that
retain their own wrapper and operand constraints. These are visibility queries,
not substitutes for a family's arithmetic, atomic, trivial-copy or VLA rules.

Ordinary, integer, miscellaneous, floating, math, query, atomic, generic-vector,
bit-cast and varargs actions carry the same record. Overload resolution wraps it
as `CheckOverloadContext.semantic`; scratch accounting is separate. A family
adapter copies `.context = input->context`, not a selected list of fields. Adding
a context field therefore reaches the next ordinary operand conversion without
editing every family adapter. The three boundary constructors still need an
explicit policy for genuinely new source data.

Two deliberately different interfaces do not acquire this record:

* The x86 constraint action receives an already-checked typed call. Its
  `constant_evaluated_context` selects immediate-constraint diagnostic policy;
  it does not perform lvalue conversion or authorize initializer reads.
* The zero-argument floating-rounds action is an effectful runtime query. It
  cannot become a source constant by receiving an initializer view.

Typed-overflow and C11/legacy atomic subfamilies use the atomic adapter. Query
proof lanes copy the ordinary context, then explicitly override the policies
that differ for that proof. Operand-selection masks, speculative proof success,
source builtin constant values and ordinary runtime values remain separate.

## Result transfers

`c_check_provenance.c` is the common home for shared eligibility predicates and
transfers. A known `CheckConstantKind` payload is not proof of legal source
syntax. The shared evaluated-constant predicate is only a prerequisite: an
integer context still checks its ICE/GNU policy, a static initializer still
checks its symbolic address, and vector writers still check representations.

| Operation | Transfer |
| --- | --- |
| Initializer receipt read | Produces GNU object-read provenance, never literal-zero spelling, a standard ICE, or null-pointer syntax. Symbolic pointer restrictions survive. The context must authorize the read; mutable/volatile/inactive/restricted accesses cannot manufacture it. |
| Arithmetic and conversion | Keep evaluated effects/faults. Cast form witnesses come from the original operand; out-of-range floating saturation vetoes ICE status. A computed zero is not a literal zero. |
| Conditional/short circuit | Only evaluated operands contribute faults/effects. An unselected arm's syntax may still affect eligibility; type checking it does not evaluate it. |
| Comma/discarded operand | Both operands are evaluated. Source evaluation records an unevaluable discarded operand even when the right value is known. Runtime evaluation retains the left modification/effects. |
| Address/dereference | Forming an inactive union member's address is not reading it. Dereference restores the inactive-read evidence. Neither operation clears a restricted pointer path. |
| Builtin | Only a family's explicit legal-fold certification can grant ICE status. Generic known-value propagation cannot. Runtime stores and atomic/volatile loads retain effects. |
| Transparent source selection | `VALUE` and `SELECT` may retain literal spelling; calls, casts and computed operators cannot. |

`constant_evaluated` is receipt-read permission, not a combined encoding of
unevaluated, discarded, unselected and runtime-foldable expressions. Deferred
faults retain their original diagnostic node, and the consuming context decides
whether the fault matters. The refactor does not change the graph's selective
publication, replay or lane ownership protocols.

## Memoized type transitions (#1050)

Do not cache a contextual action result using operand TypeIds alone. A reusable
transition must either prove independence from, or key/partition by, the source
cut and histories, extension/system-header policy, floating environment,
receipt-view identity and permission, and any value/provenance facts it reads.
A context pointer is not a stable cache identity: temporary records can reuse an
address. Type completeness and other mutable type-table state also need the
existing epoch/ownership contract. The context-free pointer-stride query no
longer receives a dummy action; this does not make its type-table dependencies
immutable. This change does not implement the memoization algorithm.

## Validation and cost

`test-checker --const-values` and the full checker suite run the new unit and
source fixtures. The unit fixtures interleave two receipt views with identical
object ids across 23 rows, all batch partition sizes, repeated calls, source
cuts, volatile/mutable/atomic controls, inactive unions and restricted pointers.
They test graph/declaration/initializer construction and overload round trips,
plus independent expected provenance/effect results and exhaustion behavior.

The source matrix uses the same nested integer/miscellaneous builtin expression
in bounds, enumerators, cases, bit-field widths, assertions, static initializers,
builtin arguments, unevaluated operands and runtime code. Const/mutable/volatile
variants have independent Clang acceptance expectations. Additional fixtures
exercise selection, discarded reads, union activity and pointer restrictions;
an execution oracle distinguishes body-local constants and runtime volatile and
atomic operations. Existing deferred/lane, typed-output and diagnostic tests
remain part of the full qualification and are not replaced by this matrix.

On the local x86-64 Clang 17 structural/focused-test build, `CheckContext` is 48
bytes. `CheckAction` remains 400 bytes, `CheckValue` 96, `CheckGraph` 432 and
`CheckStream` 400. Some bounded temporary builtin records grow: integer 352 to
368 bytes, floating/atomic 80 to 88, bit-cast 64 to 96 and vector 80 to 104;
miscellaneous remains 96. These layout observations are not performance evidence.
The focused harness passes 2,458 checks with baseline/native builds and address
plus undefined-behavior sanitizers. It is not a substitute for the pinned
Clang 23.1.4 oracle and the repository's full hosted/native qualification.

Issue closure still requires representative locked-9700X whole-checker and
whole-compile time/memory comparisons against the same prerequisite baseline.
Retain raw measurements and harnesses in `buster-performance`, not this source
repository. No throughput improvement or cost neutrality is claimed from the
source/layout changes alone.

The context/provenance code, tests and this document are original project work.
The source matrix is generated by its C test from the declarations and contexts
above; Clang is an external behavioral oracle, not an imported implementation.
