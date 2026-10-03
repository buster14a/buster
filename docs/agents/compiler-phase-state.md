# Compiler phase and state graph

[Agent instructions](../../AGENTS.md) · [Frontend reference](frontend.md) · [Machine backend](machine.md)

This is the ownership map for issue #90 from baseline `a947bc1d0` through
the first extracted boundaries. The arrows
describe dependencies, not additional compiler passes.

```text
preprocess + parse
  -> c_lower_to_ir_with_options
       capacity plan -> source/type tables -> entity and symbol resolution
       -> global initializers -> reachable function bodies -> final aliases,
          initializer registrations, diagnostics and IR certification
  -> IrProgram + IrModule (translation-unit arena remains live)
  -> codegen_generate_canonical_module_with_trace
       target/ABI preparation + canonical IR validation
       -> per-attempt global layout/images + module capacity reservation
       -> each function:
            machine selection -> optional MIR verification
            -> placement + optional quality scheduling
            -> encoding, relocations, unwind and debug publication
       -> remaining assembly, relocation check, code image publication
  -> CodegenModule
```

## Current ownership and intended boundaries

| Boundary | Input and state it owns | Output or invariant | Failure ownership today |
|---|---|---|---|
| Frontend capacity planning (`c_ir_lower_capacity_plan`) | Final token and parse counts; no IR allocation | Bounded type, symbol, function and query capacities | The caller returns an empty result; no phase identity in the public result yet |
| Frontend type and symbol setup | Parse tables, source map, type map, entity symbols, scratch indexes | All source IDs and needed symbols exist before any initializer or body names them | Diagnostics and early empty returns are shared with later phases |
| Frontend global lowering | Settled type table, entity symbols, constant evaluator | Global bytes and relocations refer to canonical symbols | Diagnostics share the result and builder state |
| Frontend function lowering | Reachability worklist, module-wide dense row streams, per-definition builder and temporary lowering arena | Function bodies become canonical IR; failed definitions do not certify the module | Diagnostics and arena cleanup are inside the long driver loop |
| Frontend publication | Finished globals and functions, aliases and initializer registrations | `canonical_ir_certified` only after all typed builders succeed | Late diagnostics are accumulated in the same result |
| Native validation and preparation | Program ABI contexts, module IR, target and options | `ir_prepare_canonical_module` accepts the module before backend consumption; predeclared assembly labels stabilize ELF call references | Wrapper returns `CODEGEN_ERROR_INVALID_IR` or capacity |
| Native global layout (`codegen_layout_globals`) | Validated globals, section groups and target layout | Indexed global descriptors, three initialized data images and two zero-fill extents; the caller then appends initializer relocations | Helper returns `CodegenError`; attempt arena owns any partial images on error |
| Native module planning (`codegen_plan_module_capacity`) | Function IR, per-type slot costs, assembly lengths and debug option | Pure instruction, frame, assembly and debug capacity plan; the caller reserves relocation, line and code buffers | Helper returns capacity or invalid IR without publishing a partial plan |
| Native function ABI/storage | One validated function, target ABI and allocator option | Machine placement frame and call layouts | Machine selection and placement own the function descriptor |
| Machine selection, scheduling and allocation | Canonical function and machine scratch arena | Selected MIR and a valid placement; scheduler is conditional under QUALITY | A structured `CodegenError`, precise refusal and active phase for every failed machine function |
| Encoding and publication | Placement, module code buffer, relocations and debug sink | Retained bytes, unwind, line and debug rows agree on code offsets | Retry rewinds the entire attempt for code-buffer exhaustion; other failures return |

The per-attempt arena owns global images, descriptors, relocations, code bytes
and debug arrays. The wrapper owns the ABI and slot-cost caches across attempts;
the machine path uses a per-function scratch arena. A failed attempt is discarded
by rewinding the arena. Every retained native allocator spelling uses MIR;
`none` aliases MIR_STACK.
A machine failure fails the complete module. The wrapper clears unpublished
code, data, relocations and debug/unwind tables while preserving diagnostics,
attempted-work counters and the active failure phase. Moving a boundary must
keep the machine path's flat iteration and data layout.

The public native result identifies a failing function, instruction and opcode
for many backend errors and names the active native owner in
`CodegenModule.failed_phase`. A successful result reports `CODEGEN_PHASE_NONE`;
the retained legacy fallback counters stay zero. The frontend has no comparable phase error field, and neither side yet reports
per-phase time and arena high-water. Those require more explicit boundary
results rather than inferring success from shared partially mutated arrays.
