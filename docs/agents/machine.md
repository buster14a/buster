# Machine instruction selection and scheduling

[Agent instructions](../../AGENTS.md) · Paths and commands below are relative to the repository root.

## Machine instruction selection and scheduling

- `MachineInstruction` is the 24-byte hot row. Keep static form, scheduling,
  memory, bundle, fixed-register, tie, early-clobber, implicit-physical, and
  implicit-resource facts in `MachineOpcodeInfo`, accessed through the
  `machine_opcode_*` helpers.
- `MachineOpcodeInfo` retains a 96-byte stride. Operand and allocation
  constraints occupy its first 32 bytes; diagnostic names follow scheduling
  and implicit-effect metadata. Keep opcode initializers designated and the
  layout checks intact. Simple FAST rows use the separate 16-byte
  `MachineOpcodeRow` projection instead of loading the full descriptor.
- `MachineFunction` owns CFG edges, block parameters, and incoming edge
  parallel-copy sources. Edge source `i` maps to destination block parameter
  `i`; keep these copies parallel through allocation so cycles are resolved as
  copies rather than serialized selector moves. Replay files include all three
  arrays and use the current replay version.
- `machine_verify_function` checks required side-table storage before reading
  rows, then reference bounds, operand kinds/classes, and payload-indexed tables.
  `operand_info` uses two role bits, three register-class bits, and three
  `MachineOperandShape` bits; give every active operand a shape. This preserves
  both the 24-byte row and the opcode record size. `VA_ARG` result operands
  admit registers or frame slots, with the side row selecting the valid kind.
  Physical references must fit `MACHINE_TARGET_REGISTER_LIMIT` and the active
  target's file; `vector_register_mask` describes class membership including
  nonallocatable registers. Target-less synthetic functions still accept
  bounded physical references without imposing a target class map.
- Stack alignments and call-target reference forms remain optional, defaulting
  to eight and DIRECT. Line marks permit duplicate rows and a final row equal
  to `instruction_count`; zero-row lowering can produce both. Validate every
  switch-case target, even an unused row, because FAST consumes the whole table.
  Keep these checks at the existing verification boundary; selector-certified
  fresh functions continue directly to placement without another verifier pass.
- An ordinary machine virtual register has exactly one definition and every
  use, including an edge-copy source, is dominated by it. The temporary
  `MACHINE_VIRTUAL_REGISTER_FLAG_MUTABLE` exception is explicit and counted;
  FAST/QUALITY liveness scans all textual touches, the scheduler preserves
  their source order, and SSA-only consumers must reject mutable values.
- The opcode switches in `machine_select_canonical_function_x86_64` and
  `machine_a64_select_instruction` are the authoritative machine selections.
  Add a selection to the target switch and its direct helpers, with MIR and
  generated-code regressions; do not add a parallel matcher that reports a
  rule without producing the selected MIR. `machine_select.{c,h}` owns only
  consumed type/value facts, row layout, and the unvalidated entry's shape
  check. The canonical IR verifier remains the pipeline validation authority.
  Unsupported machine selections return `supported = false` and
  `failed_opcode`; `CodegenStatistics.fallback_opcode_counts` and
  `fallback_verify_count` expose the actual canonical fallback. There is no
  declarative pattern-miss category because there is no declarative matcher.
- Shared canonical-IR facts and the generated FAST/QUALITY rule decision tree
  live in `machine_select.{c,h}`, `machine_select_rules.h`, and
  `machine_select_generated.c`. Target selectors may retain custom ABI and
  complex lowering, but must consume shared facts instead of introducing a
  third permanent graph IR.
- `MachineSelectResult.signature_rejected` is set only inside target function
  signature gates; other unclassified selection failures remain distinct.
  Native dispatch records exactly one `CodegenFallbackReason` per discarded
  machine function, retaining separate selection-opcode and post-selection
  counters. The driver can require zero fallback with `-fno-machine-fallback`;
  see the [driver guide](driver.md) for the curated CI corpus and reason names.
- x86 ADD/SUB/AND/OR/XOR/IMUL rows are three-operand machine SSA with operand
  0 tied to operand 1. Allocators satisfy the physical two-address constraint;
  selectors must not reintroduce a MOV plus mutable USE_DEFINE result.
- QUALITY scheduling remains pressure-first and deterministic. Pressure is
  counted per register class; metadata supplies barriers, memory membership,
  and vector scheduling membership while compatibility opcode classifiers
  cover legacy rows during migration.
- Memory scheduling uses whole-stack-object alias classes only when the
  selector's existing canonical walk certifies no volatile access in the
  function. Unknown/manual/structural-replay functions default to the original
  all-memory chain; replay intentionally drops this performance-only proof.
  A producer adding volatile accesses must clear `nonvolatile_memory_certified`.
  Known scalar frame forms and x86 512-bit frame transfers qualify only after
  their slot id and byte range are checked. Overlapping and disjoint ranges
  within one slot stay ordered. Pointer, aggregate-copy, incoming-argument, and
  unrecognized memory rows flush all pending slot chains; calls, atomics,
  fences, and physical-register rows retain their full barriers. Explicit
  mutable-vreg touch ordering remains necessary for target-promotion fallback.
  The dependency builder uses epoch-stamped slot tails and a compact pending
  list, with at most 9N+8 edges for N rows and source-order fallback before a
  scratch count can overflow. No alias classification runs in the FAST tier.
- `-fPIC` is a code model, not an accepted flag. It reaches code generation as
  `CodegenModuleOptions.position_independent`, and generation resolves it for
  the target: x86-64 ELF, where the relocations it changes are the ones `ld`
  refuses in a shared object. A symbol another object could interpose --
  `ir_symbol_is_interposable`, which is external or imported linkage without
  hidden visibility -- has its address loaded out of its GOT slot
  (`R_X86_64_GOTPCREL`) instead of computed rip-relative, and a direct call to
  one is relocated `R_X86_64_PLT32` so the linker may route it through a
  procedure linkage entry. Internal and hidden symbols keep the rip-relative
  form, and a thread-local address is the thread-local model's to pick --
  `codegen_thread_local_model` reads the same flag and answers general-dynamic
  under it. The canonical emitter and the machine path make
  the same decision from the same predicate: the selector writes a
  `MachineSymbolReference` beside each call-target row and the module
  relocation is derived from it, so the four allocators cannot disagree. One
  object-writer decision follows from the model rather than from a relocation:
  an unwind record's function pointer is relocated against a local text symbol
  with the function's own offset, because an FDE naming a preemptible function
  is the same PC-relative reference to an interposable symbol that `ld`
  refuses in the body.
- `-fPIE`/`-fpie` stay accepted and inert, and that is a statement rather than
  an omission: every reference this compiler emits is already rip-relative, an
  executable's own definitions are not interposable, its references to another
  image's data are what the linker's copy relocation is for, and its own
  thread-local block is still the initial one -- so the
  position-independent-executable model asks for no code this compiler does not
  already produce. `-fno-pic` clears the model; `-fno-pie` clears nothing
  because nothing was set.
- The built-in linker resolves both forms for the image it writes, which binds
  every name in it: `PLT32` patches the same rel32 `PC32` does, and a GOT load
  is relaxed back into the `lea` it would have been (`link_x86_relax_got_load`),
  the same relaxation `ld` performs for a `GOTPCRELX` it can resolve. The ELF
  reader takes `R_X86_64_GOTPCREL`, `GOTPCRELX` and `REX_GOTPCRELX` as one
  kind for that reason, so a `-fPIC` object -- this compiler's or clang's --
  links here. An instruction shape the relaxation does not recognize fails the
  link by name rather than being rewritten. It relaxes the two indirect
  thread-local models back to local-exec for the same reason
  (`link_elf_relax_thread_local`).
