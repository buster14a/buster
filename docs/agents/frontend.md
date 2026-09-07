# C frontend and canonical IR reference

[Agent instructions](../../AGENTS.md) · Source paths are relative to the repository root.

Search the guide matching the affected symbols or behavior. These notes preserve
the original investigation order; cross-references to “above” or “below” follow
the table. Historical failure descriptions are evidence to reproduce, not a
substitute for inspecting the current implementation and fixtures.

| Area | Guide |
|---|---|
| Pipeline, ownership, diagnostics, canonical IR, expression places and qualifiers | [Foundations](frontend/foundations.md) |
| Relocations, weak/alias symbols, constructors/destructors, object formats and linker | [Linkage](frontend/linkage.md) |
| Packed/aligned types, bit-fields, layout engines | [Layout](frontend/layout.md) |
| Atomic layout, argument classification, loads/stores, conversions | [Atomics](frontend/atomics.md) |
| Declarators, typeof, conditional types, calls, JIT/driver boundaries | [Calls](frontend/calls.md) |
| Long double, x87, static folding, global/inline assembly, Wasm boundary | [Wide floats and assembly](frontend/wide-floats-assembly.md) |

Layout and atomic ABI work often needs both the layout and atomics guides.
Changes to places or calls also need the foundations guide. Native selection and
allocation invariants live in [the machine guide](machine.md); command-line
options and action dispatch live in [the driver guide](driver.md).
