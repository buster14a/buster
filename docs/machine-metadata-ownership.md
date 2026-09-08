# Machine metadata ownership

[Machine backend guide](agents/machine.md) · Issue [#45](https://github.com/buster14a/buster/issues/45)

This inventory covers `MachineOpcodeInfo`, its published row projection, and
every `MachineFunction` field. It was checked against `ebfdd117` plus the
scheduler-membership change accompanying this document. It describes actual
producers and consumers; an accessor with no callers is not an active consumer.
After integration with `88ecc49c`, the operand metadata also carries the
operand-shape facts published by the stricter machine verifier.
Integration with `10fa0435` preserves the descriptor layout from PR #138:
operand and allocation constraints occupy the first 32 bytes, scheduling
metadata follows, implicit physical/resource masks begin at offset 48, and
the diagnostic name begins at offset 80. The stride remains 96 bytes; layout
assertions in `machine.h` enforce these boundaries.

## Static opcode descriptors

`machine_opcode_infos` in `machine.c` is the sole producer. Its designated
initializers and `MACHINE_INFO_*` macros define process-lifetime `const` rows.
No function owns a mutable copy, no instruction caches a descriptor pointer,
and an opcode change selects another row. Recompiling a changed table is the
invalidation boundary; there is no per-function invalidation operation.

All fields below have that lifetime. “Dormant” means the audited source has
no production consumer, even when a value or accessor exists. This change
does not remove those fields or promise that future consumers may trust them.

| Field | Status and current consumer | Producer detail / outstanding constraint |
| --- | --- | --- |
| `name` | Dormant populated diagnostic name | Static string initializer; no in-process consumer in the audited tree |
| `operand_count`, `operand_info[4]` | Authoritative inline operand count, role, class and shape; verifier, scheduler, allocators; count and roles are projected into row facts | The target row defines the machine operation, including expansion scratch constraints. Each operand byte uses two role bits, three class bits and three shape bits; the new float-move helpers inherit register shape through the shared operand constants |
| `tied_pair` | Authoritative destination/source tie; MIR_STACK and FAST placement, verifier/tests | Two-address SSA rows; changing an opcode requires using its own tie |
| `early_clobber_mask` | Live constraint input, currently zero in every descriptor; FAST/QUALITY constraint detection | No current nonzero producer; the separate `machine_opcode_operand_is_early_clobber` accessor has no caller |
| `fixed_register_set` | Live but redundant constraint-presence input; `machine_opcode_has_constraints`, QUALITY | SHIFT/DIV/MULH rows also have explicit `fixed_register_mask` and `fixed_registers`; removing this legacy input needs its own compatibility check |
| `fixed_register_mask`, `fixed_registers[4]` | Authoritative explicit physical operand assignments; verifier and all machine placement modes | A mask bit controls whether the corresponding register byte is meaningful; target-specific scratch fallback remains for other rows |
| `attributes` | Authoritative call, terminator, flags, constraint and rematerialization facts; verifier, scheduler and placement | `MEMORY` overlaps `memory_effect`; `BUNDLE`/`EXPANDS` do not currently drive bundle/expansion consumers |
| `clobber_mask` | Authoritative extra physical-register clobbers; all placement modes and compact row projection | Encoder-sequence scratch beyond explicit operands; also determines required callee saves |
| `schedule_class` | Authoritative scheduler barrier/vector membership where specified; published into `schedule_flags` | ALU/SHIFT/MUL/DIV/LOAD/STORE labels are not currently latency or throughput inputs; no scheduling cost model consumes them |
| `memory_effect` | Authoritative conservative memory-chain membership; `machine_opcode_is_memory`, row publication | Includes six aggregate copies and the x86 incoming read formerly described only by a scheduler switch. It is not a complete hardware-memory-effects model: existing side-effect barriers can omit it |
| `implicit_resource_uses`, `implicit_resource_defs` | `VECTOR_STATE` is authoritative implicit vector-state chain membership; row publication | Float bridges, scalar conversions and AArch64 implicit V-register rows. Existing FLAGS/NZCV bits still have no resource-mask consumer; flag ordering currently uses `attributes` |
| `form_set` | Dormant populated field | `machine_opcode_form_set` has no caller; no selector or encoder consults this field |
| `expansion_recipe` | Dormant populated field | `machine_opcode_expansion` has no caller; actual emission recipe identity comes from the independent registry described below |
| `memory_operand` | Dormant populated field | Slot-plus-one accessor has no caller; must not be treated as a validated alias or folding description |
| `bundle` | Dormant, zero | Accessor has no caller; scheduler flag-pair units are built from attributes, not this byte |
| `memory_fold_alternate`, `emit_recipe`, `memory_flags`, `latency`, `throughput` | Dormant, zero | No producer beyond zero initialization and no consumer. The in-record `emit_recipe` is not the authoritative recipe projection |
| `implicit_physical_defs` | Dormant, partly duplicates `clobber_mask` | Populated by constrained conversion/DIV/MULH macros but never read |
| `implicit_physical_uses` | Dormant, zero | No nonzero producer or consumer |
| `reserved_hot`, `reserved_metadata`, `reserved_schedule[4]` | Padding, not semantic state | Zero; preserve the descriptor layout and must not be consumed |

The independent `machine_opcode_emit_recipes` / x86 emit registry supplies
`machine_opcode_emit_recipe()`. It is static, read-only, keyed by stable
`MachineOpcode`, and consumed by recipe/encoding registry audits. The
`MachineOpcodeInfo.emit_recipe` member is not a second valid lookup route.
Changing this relationship or the integrated descriptor layout is outside
this patch.

## Published row facts

`machine_opcode_rows_once` derives `MachineOpcodeRow` from descriptors and
the encoder's opcode-specific capacity policy. `machine_opcode_rows_prewarm`
publishes the table at the existing serial prewarm boundary before worker
use; `BUSTER_CHECK_SERIAL_INITIALIZATION` guards first construction and the
readiness flag is written after the complete table. The backing records are
private, and clients receive only a `const` pointer. The immutable descriptor
table cannot become stale during a process lifetime, so no per-row generation
counter or additional validation pass is needed.

| Field | Producer | Consumer / lifetime and invalidation |
| --- | --- | --- |
| `clobber_mask` | Descriptor clobbers | FAST prepass; process lifetime, rebuild with descriptor table |
| `role_lanes`, `operand_count` | Descriptor operand count and roles | FAST/QUALITY prepass; same boundary |
| `flags` | Descriptor constraints, attributes and clobbers; explicit indirect-branch/variable-budget opcode policy | Allocator prepass and x86 capacity calculation; same boundary |
| `encode_budget` | Existing bounded x86 row-capacity policy | x86 encoder capacity calculation; same boundary |
| `schedule_flags` | Descriptor barrier/memory predicates and implicit vector-state masks | Scheduler unit construction; same boundary |
| `reserved` | Zero initialization | Padding; no consumer |

`schedule_flags` occupies one previously reserved byte. `MachineOpcodeRow`
stays 16 bytes; the descriptor stays 96 bytes on this 64-bit host. There is
no new allocation, per-instruction field, or scheduling pass. The scheduler
adds physical-operand barriers dynamically because that fact belongs to an
instruction rather than an opcode. Existing unknown-opcode rejection runs
before the projected row is indexed.

The whole-domain regression freezes all 236 opcode memberships: 42 barrier,
45 memory-chain, and 33 implicit-vector-chain members, including overlaps.
An AArch64 vector frame load retains `LOAD` classification and belongs to
both memory and implicit-vector chains. An explicit x86 virtual vector ALU
row relies on its virtual-register dependencies; it is not added to the
implicit scratch chain merely because the hardware operation is vectorized.

## Function-owned arrays and metadata

Selectors construct a function in a temporal arena. Builder streams become
flat published arrays through `machine_function_builder_finish`; same-arena
single-chunk materialization can alias a finished stream. Otherwise
materialization copies to the destination arena. Retain that arena and the
canonical IR/source context until allocation, encoding, relocation and debug
consumers finish. No function array is independent heap ownership.

Every pointer below is paired with its listed count. Counts bound the live
span, not arena capacity. A transformation replacing a span must publish
the pointer and count together and rebuild dependent instruction/block IDs
before a consumer runs.

| Field(s) | Status / producer | Consumer and invalidation |
| --- | --- | --- |
| `instructions`, `instruction_count` | Authoritative target rows; selector builder | Verifier, scheduler, placement, encoder and replay. Changing order invalidates definition points, line marks, block row spans and row-indexed placement/encoding data |
| `virtual_registers`, `virtual_register_count` | Authoritative class/SSA identity plus derived definition points; selector builder and pre-publication compaction | Verifier, scheduling and placement; compaction remaps row operands, block parameters and edge sources. Scheduling remaps definition points in a fresh array |
| `blocks`, `block_count` | Authoritative block layout/parameter spans; builder; edge-splitting publication | Verifier, CFG scans, scheduling, placement, encoding and replay; block insertion requires instruction/edge/switch remapping. `frequency_class` is derived after selection and must be restamped after a CFG change |
| `edges`, `edge_count` | Authoritative machine CFG with parallel-copy slices; selector and edge splitting | Verifier, liveness and edge-copy placement; CFG/block/parameter changes invalidate this representation |
| `block_parameters`, `block_parameter_count` | Authoritative destination parallel-copy order; selector builder | Verifier and placement; changing order requires matching every incoming edge-copy source slice |
| `edge_copy_sources`, `edge_copy_source_count` | Authoritative incoming parallel-copy values; selector builder | Verifier and placement; remap with virtual-register IDs and incoming-edge parameter changes |
| `immediates`, `immediate_count` | Authoritative selected literal pool; target selector stream | Reference validation, rematerialization and encoders; changing pool indices requires operand/edit remapping |
| `stack_slot_sizes`, `stack_slot_alignments`, `stack_slot_count` | Authoritative selected storage requirements; target selector | Reference validation and frame placement; changing size/alignment/identity invalidates placement offsets. Missing alignment array uses the existing eight-byte compatibility default |
| `call_targets`, `call_target_references`, `call_target_count` | Authoritative symbol IDs and PIC relocation choice; target selector; x86 publishes parallel arrays | Module codegen resolves encoded call-site indices into symbol relocations. Null reference array means the existing DIRECT model, including AArch64. Symbol linkage/code-model changes require reselection |
| `switch_cases`, `switch_case_count` | Authoritative switch comparisons or indirect-branch target set; selector; edge splitter remaps block IDs | Verifier, CFG/liveness/frequency walks and encoders; changes invalidate CFG-derived facts and any prior encoding |
| `line_marks`, `line_mark_count` | Debug provenance only; selectors map machine row starts to canonical instruction IDs | Module debug-line emission resolves source ranges on demand. Scheduler copies, remaps and sorts marks; edge splitting remaps them. Retain canonical source context |
| `va_args`, `va_arg_count` | Derived ABI classification, authoritative once selected; target selector | x86/AArch64 VA_ARG encoders consume `size`, `alignment`, `stack_size`, `part_count`, `parts`, `result_slot`, `result_is_frame`, `scalar_size`; each part carries `value_offset`, `save_offset`, `size`, `is_float`, `is_memory`. Reserved bytes are padding. Type/ABI/stack-slot changes require reselection |
| `target` | Borrowed immutable target/register-file descriptor; selector | Verifier, placement, scheduling and encoders; static lifetime. Changing target requires reselection and placement rebuild |
| `outgoing_bytes`, `outgoing_slot` | Derived fixed outgoing call-area requirement and selected slot; target selector | Placement pins the slot to the frame bottom; AArch64 store emission respects its base. ABI/call-argument changes invalidate frame placement; zero bytes makes the slot irrelevant |
| `nonvolatile_memory_certified` | Performance-only selector proof that the existing canonical walk found no volatile memory access | Enables bounded stack-slot alias classes in scheduling. Unknown/manual functions default to false; structural replay deliberately drops the proof. Producers introducing volatile accesses must clear it; row reordering and CFG/SSA rewrites preserve it only while they add no volatile access |
| `reserved[7]` | Padding | No semantic producer or consumer; preserve the `MachineFunction` layout |

Scheduling shares arrays other than instructions, virtual registers and line
marks with its input. Its candidate and original must retain the input arena.
The accepted candidate must receive newly built placement: edits, row-indexed
operand registers and encoder offsets cannot be reused after reordering.
These are existing ownership rules, not newly introduced mutable caches.

The replay format serializes only instructions, virtual registers, blocks,
edges, block parameters and edge-copy sources. It does not persist immediate,
stack, call, switch, debug, vararg, outgoing-area or target context; it is a
core structural test snapshot, not a complete backend-ready function format.
Replay also clears `nonvolatile_memory_certified`, retaining conservative
memory ordering without canonical volatile provenance.
Deserializer success establishes framing only; manually built or replayed
functions still need structural verification before backend consumption.

## Outstanding issue #45 work

This patch completes scheduler membership authority and removes its three
legacy opcode classifiers. It deliberately leaves #45 open:

- Remove the dormant descriptor fields and uncalled accessors above in a
  separate change coordinated with descriptor-layout and verifier work;
  record the actual static-table and compiler-build impact. No owner or
  future feature is invented to justify keeping them indefinitely.
- Resolve the redundant constraint-presence and memory predicates with
  whole-domain comparisons before removing either source. Do not treat
  populated FLAGS/NZCV/physical masks as an implemented hazard model.
- Extend the field census beyond the scopes above before claiming that all
  retained machine metadata is active. For example, virtual-register
  `rematerialization_recipe` and `hint` have no production producer/consumer;
  `typed_origin` is selector-written provenance retained by raw replay, with
  no in-process semantic consumer in the audited tree.
- Coordinate removal of the uninstantiated `MachineAddress`,
  `MachineSegment`, `MachineUse` and `MachineLocationSegment` declarations
  with PR #256; this patch does not duplicate it. Keep encoded reference
  tags stable.
