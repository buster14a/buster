# Nested sizeof preparation

Issue #822 concerns the parser-to-checker reader, not the parser's bracket
handling. Folding each operand again in the expression graph's postorder walk
makes a chain of N `sizeof` operators visit the sum of their N operand slices.

`check_input_sizeof_prepare` now runs after binding and selection resolution,
after constant-context marking, and before `check_input_expressions`. Its
bounded 64-token classifier uses the reader's existing SIMD scan and exact
scalar twin. It visits outer queries in source order. One enclosing expression
fold publishes each nested query's operand type as it executes that query's
constant action; subsequent graph construction consumes those types directly.
For a non-variable-size outer operand, the preparation pass suppresses warnings
once over that operand and skips the enclosed queries in its source-order scan.
The already-indexed warning machinery still excludes independent type-name
bounds. A variable-length operand does not suppress its evaluated work.

## Handoff and ownership

`CheckInputReader.sizeof_types` is a zero-default, token-indexed `TypeId` column
in the reader's sparse arena. It lives only for that reader invocation. A
nonzero entry supplies the operand type for the existing variable-length-array
routing decision; zero preserves the original fallback for an unsupported or
unresolved operand. The explicit-fold counters in `CheckInput` count calls and
input tokens, including fallbacks.

Only the preparation pass enables `sizeof_capture`. Earlier declaration folds
and deferred checker lanes must not publish into this column: their type IDs
can still require remapping. No binding-state-dependent cache survives the
reader, and no hash lookup or slice allocation is added per query.

This is deliberately separate from `query_types`. That column identifies
explicit type-name queries and controls whether the graph retains an expression
operand. Populating it with inferred expression types would incorrectly bypass
operand constraints, notably `sizeof` on a bit-field. The graph and its existing
semantic checks remain intact.

Before graph construction, binding has published a parenthesized type-name
query at its closing token. Preparation reads it there rather than re-reading
the type slice. This avoids replaying independent array-bound warnings after
selection suppression; graph construction forwards the column to the prefix
as before.

## Complexity and tests

A supported nested expression chain is folded once, over its outermost operand.
A flat forest folds disjoint operands. The additional source scan and cache
stores are linear in tokens; there is no recursive traversal or per-query
operand rescan. The existing fallback for unsupported expressions is retained;
this does not claim a new complexity bound for arbitrary rejected programs.

`build/compiler test-checker --sizeof` exercises generated depths through
16,000 on both reader paths, exact fold counts, a linear bound on folded tokens,
every retained query's value, and scalar/vector typed-stream equality. The five
forms are nested expression operands, an innermost type name, arithmetic between
nested queries, flat queries, and one query around deep ordinary parentheses.
Boundary depths include 21/22 and 63/64/65. The command also runs the existing
integer-warning and variable-length-array suites. The generated regression is
part of the default checker suite; it has no machine-dependent timing limit.

Wall-time qualification, compiler/version pins and raw measurement artifacts
belong in `buster14a/buster-performance`, not this source repository.
