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
metadata follows, reserved physical-mask bytes begin at offset 48, active
resource masks begin at offset 64, and
the diagnostic name begins at offset 80. The stride remains 96 bytes; layout
assertions in `machine.h` enforce these boundaries.

## Authority boundaries checked for A12

Checked at `ef99ec72cf0f17c8818a1d4b4c106fd5908b5eae`. This is a source
ownership map, not a claim that every retained field is consumed or that every
listed target can execute every operation. Paths below are under
`src/buster/lib/` unless stated otherwise.

| Family | Existing authority | Derived data or remaining independent policy |
| --- | --- | --- |
| Machine opcode identity | `MachineOpcode` in `compiler/codegen/machine.h`; `machine_opcode_infos` defines each operation's operand contract | Compact `MachineOpcodeRow` roles, flags and scheduler membership are published once from descriptors. A machine operation is not an exact ISA form. |
| Instruction forms | `compiler/assembly/x86_64_metadata.{c,h}` and its generated snapshot own normalized x86 forms, bindings and encoding fields | Exact tokens, shape caches and templates are snapshot-derived. Assembler sizing/legality still has independent decisions; that remaining #267 slice is not completed here. |
| Operand effects | Active descriptor roles, shapes, ties, fixed-slot bindings and clobbers; instruction operands supply dynamic references | Row facts and allocator placement are derived. Implicit vector scratch has explicit chain membership; flags use the flag attributes. `fixed_register_mask` and `fixed_registers` now exclusively describe explicit fixed assignments. |
| Encoding recipes | `machine_opcode_emit_recipes` and `machine_x86_64_emit_registry.h` identify real dispatch/expansion recipes; exact bytes come from x86 metadata | `machine_opcode_emit_recipe()` is the lookup route; the dormant in-record duplicate has been removed. TLS, forwarding, GOT relaxation and padding migrations already landed; no replacement registry is introduced. |
| Target capabilities | `target.{c,h}` owns target identity and CPU features; target-specific selectors and backend gates decide implemented lowering; `MachineTargetDescription` owns the machine register file | Selected machine functions borrow immutable target descriptions. An enum or feature bit is not proof of complete frontend/ABI/object/runtime support; #309 tracks conformance reporting. |
| Module registration | `CMakeLists.txt` owns split-build module/source membership | Unity includes in `src/buster/apps/ide/ide.c`, test registration in `src/buster/tests/test.c`, and direct prewarm lists are separately maintained, not manifest-generated. #89 remains open; unity and embedded tests still default on at the checked base. |
| Binary writers | `CodeviewBuffer`/`codeview_emit_bytes`, `PdbBuffer`/`pdb_emit_bytes`, and `ObjectBuffer` own their respective mutable output state | CodeView and PDB still duplicate bounded writes (#314). A shared substrate must preserve each owner's bounds/error/lifetime contract; object-format policy is not an ISA encoding authority. |

Historical Forgejo #598 and #857 map to GitHub #45 and #89 in
[the migration map](issue-migration-map.md). The GitHub numbers in this
inventory are not remapped. PR #138's descriptor layout, #256's removal of
uninstantiated records, and #270's scheduler projection are already integrated.

## Static opcode descriptors

`machine_opcode_infos` in `machine.c` is the sole producer. Its designated
initializers and `MACHINE_INFO_*` macros define process-lifetime `const` rows.
No function owns a mutable copy, no instruction caches a descriptor pointer,
and an opcode change selects another row. Recompiling a changed table is the
invalidation boundary; there is no per-function invalidation operation.

All fields below have that lifetime. “Dormant” means the audited source has
no production consumer, even when a value exists. Removed fields are
recorded below the inventory; reserved bytes have no semantic authority.

| Field | Status and current consumer | Producer detail / outstanding constraint |
| --- | --- | --- |
| `operand_count`, `operand_info[4]` | Authoritative inline operand count, role, class and shape; verifier, scheduler, allocators; count and roles are projected into row facts | The target row defines the machine operation, including expansion scratch constraints. Each operand byte uses two role bits, three class bits and three shape bits; the new float-move helpers inherit register shape through the shared operand constants |
| `tied_pair` | Authoritative destination/source tie; MIR_STACK and FAST placement, verifier/tests | Two-address SSA rows; changing an opcode requires using its own tie |
| `early_clobber_mask` | Live constraint input, currently zero in every descriptor; FAST/QUALITY constraint detection | No current nonzero producer; the uncalled per-slot accessor has been removed |
| `fixed_register_mask`, `fixed_registers[4]` | Authoritative explicit physical operand assignments; verifier and all machine placement modes | A mask bit controls whether the corresponding register byte is meaningful; target-specific scratch fallback remains for other rows |
| `attributes` | Authoritative call, terminator, flags and constraint facts; verifier, scheduler and placement | `MEMORY`, `REMATERIALIZABLE`, `BUNDLE` and `EXPANDS` are removed; actual rematerialization comes from the FAST prepass |
| `clobber_mask` | Authoritative extra physical-register clobbers; all placement modes and compact row projection | Encoder-sequence scratch beyond explicit operands; also determines required callee saves |
| `schedule_barrier` | Explicit opcode barrier membership; row publication | Replaces the only populated and consumed schedule class, BARRIER; cost classes and unused enum values are removed |
| `memory_effect` | Sole static conservative memory-chain classification; `machine_opcode_is_memory`, row publication | Includes six aggregate copies and both native incoming reads. It is not a complete hardware-memory-effects model: existing side-effect barriers can omit it |
| `implicit_vector_state` | Explicit implicit-vector-scratch chain membership; row publication | The union of the former VECTOR_STATE uses/defs, with no read/write hazard model implied. Flag ordering exclusively uses the existing attributes |
| `reserved_constraints`, `reserved_hot`, `reserved_form`, `reserved_expansion`, `reserved_metadata`, `reserved_recipe[4]`, `reserved_schedule[9]`, `reserved_physical[2]`, `reserved_cold[31]` | Padding, not semantic state | Zero; preserve the descriptor layout and must not be consumed |

The independent `machine_opcode_emit_recipes` / x86 emit registry supplies
`machine_opcode_emit_recipe()`. It is static, read-only, keyed by stable
`MachineOpcode`, and consumed by recipe/encoding registry audits. The
removed `MachineOpcodeInfo.emit_recipe` member was never a valid lookup
route. This relationship and all retained descriptor offsets are unchanged.

## Single memory-chain authority

Checked against `b4d56a359dc55343cfa91915e4025a26b3eecce7`. The compiled table
has 246 opcodes: 38 carry the old `MACHINE_OPCODE_ATTRIBUTE_MEMORY` bit,
49 have a valid non-NONE `memory_effect`, and none depend on the bit alone.
The bit's only reader was `machine_opcode_is_memory`, which ORed it with the
effect classification. Remove the duplicate bit and all its producers; the
helper now classifies solely through `machine_opcode_memory_effect`. Other
attribute bit values, effects and descriptor offsets do not change.

The immutable table owns the classification for the process lifetime, and
`machine_opcode_rows_once` projects it into the existing memory scheduling
bit at serial publication. No extra pass, cache or invalidation scheme is
needed. Changed opcode selection must publish the corresponding effect before
consumers run, as for the other static descriptor facts. There is no new
allocation and no table-size reduction; measurements are reported separately.

The existing frozen scheduler-membership table independently checks both the
helper and effect classification for every opcode. The six aggregate-copy
operations retain READ_WRITE, and both incoming-read operations retain READ.
The alias-order regression now uses the helper without redundant exceptions
for those opcodes; its independent slot/range and conservative-copy ordering
checks remain. Null, zero, out-of-domain effects and a side-effect-only
synthetic descriptor retain their existing classification behavior.

Memory membership and barriers are different facts. Call, side-effect and
terminator attributes and explicit barrier membership still impose
barriers, including operations without a memory effect. VOLATILE, ATOMIC and
BARRIER effects also remain barriers through the existing publication logic.
This cleanup does not establish complete hardware effects or new alias proofs;
unknown memory and uncertified functions keep the current conservative policy.

## Removed dormant descriptor state

Checked against `f52360b20f73eb9258516687947dabddfa4f76f6`. The descriptor's
`memory_fold_alternate`, in-record `emit_recipe`, `memory_flags`, `latency`,
`throughput`, `bundle` and `implicit_physical_uses` had only zero initialization
and no production consumer. `memory_operand` had populated slot-plus-one hints,
but its accessor had no callers. `implicit_physical_defs` was populated by
DIV/MULH and four unsigned float-conversion rows, duplicating the active
`clobber_mask` without any reader. All nine fields and their initializer
assignments are removed, together with the uncalled bundle, memory-operand
and per-slot early-clobber helpers and `MachineBundleKind`.

In that historical slice, the live `early_clobber_mask`, fixed-slot assignments,
`clobber_mask`, memory effects, resource masks and recipe projection remained
intact; the later cleanup below reduces resource masks to their sole live fact. The machine
suite checks the clobber projection across the whole opcode domain and the
specific RDX and RCX/ZMM0/ZMM1 scratch contracts independently of the removed
duplicates. Existing memory-chain, vector-state, constraint, registry and
encoding tests continue to cover their consumers. Reserved bytes preserve
the 96-byte descriptor and every retained offset; the 24-byte instruction
and 16-byte row projection are unchanged. There is no static-table byte
saving, new allocation or pass. Compiler-build and runtime evidence belongs
to the accompanying performance audit and exact-revision validation results;
removing declarations alone does not establish a speedup.

That earlier slice left names, attribute/resource bits and the wider census
open. The final cleanup and complete shared-record census below resolve them.

## Removed unused form and expansion identities

Rechecked against `5324b7d077d5727a23dd680c0a3efb778a55ff7f`: the descriptor
`form_set` and `expansion_recipe` had initializer producers and accessor
implementations, but no accessor callers or direct consumers anywhere in
`src/`. They did not select a legal encoding, describe an actual expansion,
or supply scheduler membership. Remove both fields, their producers, their
accessors, the three now-unused form/expansion enums, and the unused
`MACHINE_INFO_FINALIZE` macro. `reserved_form` and `reserved_expansion` are
zero-initialized padding preserving every retained descriptor offset, not
planned semantic fields. The descriptor/instruction/projection strides remain
96/24/16 bytes. Compile-time checks also freeze the active `schedule_barrier`
and `memory_effect` offsets at 32 and 36.

The existing identities have these supported joins:

| Domain | Producer and consumer | Supported join and validity |
| --- | --- | --- |
| Selected operation (`MachineOpcode`) | Target selector switches publish rows; verifier, scheduler, placement and encoder consume them | Index immutable opcode descriptors, published row facts and the recipe projection. Validate the opcode before indexing. |
| Emission recipe (`MachineEmitRecipeId`) | `machine_opcode_emit_recipes`, with the x86 registry supplying its x86 subset; registry audits consume the projection | Retain both category and index. DIRECT index 0 (`MOV_RR`) and FAMILY index 0 (`MOV_RI`) are different recipe IDs. An out-of-domain opcode returns INVALID. |
| x86 producer ordinal | `machine_x86_64_emit_registry.h`; registry audits check the matching encoder cohort | `machine_x86_64_emit_registry_entry` indexes the contiguous registered x86 span only. Its ordinal is not a machine opcode or exact form ID; an out-of-range ordinal returns null. |
| Exact x86 form | The normalized x86 metadata snapshot and checked binding/encoding helpers | The current emitter resolves a supported operand shape through those helpers. A recipe category or common mnemonic does not identify an exact form. Missing forms fail explicitly through the existing checked emission result. |
| Multi-instruction expansion | Existing target emitter policy, identified in the recipe registry | The selected machine operation must already include its sequence's operand constraints, clobbers, barriers and implicit vector-state effects. The removed SINGLE/PSEUDO labels never validated these facts. |

Descriptors and recipe data are immutable for the process lifetime. Selection
publishes the operation and dynamic operands before scheduling and placement.
Changing that operation selects a new descriptor and requires rechecking the
facts those consumers used. Exact-form decisions deferred to emission may
vary bytes or immediate/displacement width only while preserving the selected
operation's effects, ties, fixed-register assignments, clobbers and sequence
contract. Introducing a memory form, implicit scratch register, flag effect,
or expansion after those facts were consumed requires a new earlier selection
contract; matching mnemonics do not authorize it.

The bounded x86 move-family tests distinguish register and immediate recipe
identities, frame-read effects and scheduler membership, and missing registry
mappings. Existing exhaustive registry tests check category/index/status joins;
existing emitter tests check actual bytes and unsupported/capacity failures.
These checks reuse current authorities and add no production projection,
allocation or per-instruction pass. Layout preservation means zero table-byte
saving; build/runtime measurements must be reported separately, without
inferring a speedup from removing unused declarations. The remaining descriptor cleanup is covered by the final section below.

## Removed fixed-register duplication

The checked base had three macros (`MACHINE_INFO_SHIFT`, `MACHINE_INFO_DIVIDE`
and `MACHINE_INFO_MULTIPLY_HIGH`) populating `fixed_register_set` for 15 x86
opcodes. Every one also specified `fixed_register_mask == 3` and RAX/RCX in
its slot bytes. The set had only two readers, both Boolean: the shared
constraint predicate and QUALITY's pin-budget predicate. It never supplied
an assignment. Remove the field, its three producers and its two reads;
`reserved_constraints` preserves the existing two-byte layout hole without
retaining semantic state. Register zero is valid: the slot mask, not the
register byte's truth value, determines whether an assignment exists.

The two predicates intentionally remain different. The shared helper includes
tied operands; QUALITY's forced-register/pin-budget predicate does not, since
a tie alone does not force a fixed scratch assignment. Early-clobber and
attribute-only constraints remain supported. This is a verified authority
cleanup, not a claim of a miscompile, a measured speedup or lower RSS. The
96-byte descriptor, 16-byte projection, 24-byte instruction, prewarm boundary
and direct calls are unchanged. No new allocation or instruction field is
introduced.

The existing machine suite checks the helper and its row projection against
the explicit facts across every opcode, checks the 15-opcode RAX/RCX family,
and covers null/out-of-range slots, fixed register zero, ignored unmasked
bytes, ties, early clobbers and attribute-only constraints. The same regression
can run against the pre-removal source to establish predicate equivalence;
CI results must be attached to the exact revision rather than inferred from
this inventory. No independent production table is added for the tests.

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
| `schedule_flags` | Descriptor barrier/memory predicates and implicit vector-state membership | Scheduler unit construction; same boundary |
| `reserved` | Zero initialization | Padding; no consumer |

`schedule_flags` occupies one previously reserved byte. `MachineOpcodeRow`
stays 16 bytes; the descriptor stays 96 bytes on this 64-bit host. There is
no new allocation, per-instruction field, or scheduling pass. The scheduler
adds physical-operand barriers dynamically because that fact belongs to an
instruction rather than an opcode. Existing unknown-opcode rejection runs
before the projected row is indexed.

The original scheduler regression froze all 236 opcode memberships (42
barrier, 45 memory-chain and 33 implicit-vector-chain members, including
overlaps). At the A12 checked base its explicitly maintained domain has 241
opcodes; `schedule_memberships` in `machine_test.c` is the current test contract.
An AArch64 vector frame load retains READ memory effects and belongs to
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
| `windows_aarch64_frame` | Authoritative target ABI property; AArch64 selector | Stack placement and native encoder/unwind construction; preserve through scheduling and rebuild after a target change |
| `predicate_absence_certified` | Selector proof from the existing SIMD-operation census or disabled predicate-residency policy that no predicate allocation is needed | Predicate placement admission skips its private inspection. Manual/replayed functions default to false; any producer adding predicate values must clear it before placement |
| `reserved[5]` | Padding | No semantic producer or consumer; preserve the `MachineFunction` layout |
| `stack_slot_memory_flags` | Optional derived byte per frame object, built from checked canonical-to-machine source spans only for mixed volatile functions | Scheduler admits only explicit NONVOLATILE objects; verifier rejects unknown bits, replay drops the proof. Slot-preserving row/CFG rewrites may share the table. A new volatile access clears its affected object and the whole-function certificate; changing slot identities requires remapping/rebuilding |

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

## Final descriptor and value cleanup

Reconciled against main `44a90fcbff44988624878aff9db3346a8bc6550b`.
The descriptor name had no reader. REMATERIALIZABLE had initializers but
no consumer; the allocator derives literal recipes from actual defining
rows. EXPANDS had one macro producer and no reader; BUNDLE had neither.
FLAGS/NZCV resource bits duplicated the real flag attributes without driving
ordering. The only resource-mask reader ORed uses and definitions to ask
about VECTOR_STATE, so `implicit_vector_state` now states that exact fact.
The only populated schedule class with a semantic consumer was BARRIER;
`schedule_barrier` states it directly. The other cost-class assignments,
resource aliases, unused compatibility macro, unused name strings, and
now-identical initializer macros are removed. The existing independent
whole-opcode scheduler-membership oracle remains unchanged.

`MachineVirtualRegister.rematerialization_recipe` and `hint` had no nonzero
production writers. Their only readers wrote diagnostic trace slots, while
structural replay copied the entire record. They are now explicitly reserved
zero words. Bootstrap tracing emits the same zero slots to preserve its
versioned framing; the verifier rejects nonzero reserved words in manual or
replayed values. No allocator policy or nonzero diagnostic information is
removed. `typed_origin` remains genuine provenance, as specified below.

The descriptor remains 96 bytes, the instruction 24, the virtual register
16 and the published row 16. Remaining fields retain their offsets; there
is no new pass, array or allocation. Padding deliberately separates this
semantic cleanup from a descriptor-stride experiment. This is not a claim
that a 96-byte descriptor is optimal or that cleanup improves throughput.
The [final-slice audit](performance-audits/2026-09-11T194152Z.md) records
compiler-throughput and memory observations and their limits.

## Record field census and invalidation

This census covers the shared production records in `machine.h`. Target
selection scratch and exact-encoder cache records belong to their owning
translation units. The test-only audit shapes at the end of `machine.h`
are output diagnostics of the named test seams, not production state.
All arrays and indexes below are bounded by their published counts.

| Record / fields | Producer and consumer | Lifetime and invalidation |
| --- | --- | --- |
| `MachineInstruction.operands`, `payload`, `opcode`, `flags` | Selector/edge splitter; metadata-aware verifier, scheduler, allocators, encoder, trace and replay | Function arena. Opcode determines each payload/flag interpretation. Reordering invalidates points, row offsets and placements; changing a reference invalidates use/liveness facts |
| `MachineVirtualRegister.definition_point`, `register_class`, `flags` | Selector, edge splitting and scheduler point remap; verifier, liveness and placement | Function arena. Recompute defining points after reorder; compaction remaps every use/parameter/edge. MUTABLE is an explicit compatibility contract, never hidden SSA |
| `MachineVirtualRegister.typed_origin` | Native selector writes canonical value ID, synthesized values use invalid ID; selector tests and bootstrap trace read it; replay preserves it | Borrows the retained canonical value domain; remap with canonical value IDs. Not an allocator hint |
| `MachineVirtualRegister.reserved_recipe`, `reserved_hint` | Zero initialization; verifier checks zero and replay preserves framing | No semantic value. Nonzero stale metadata is rejected. Actual recipes are `MachineFastPrepass.rematerialize_immediates` |
| `MachineBlock.first_instruction`, `instruction_count`, predecessor/successor offsets/counts, parameter offset/count | Builder and edge splitter; verifier, scheduler, CFG/liveness, placement and encoder | Function arena. Rebuild/remap after any CFG or row-layout edit |
| `MachineBlock.frequency_class` | `machine_function_stamp_frequency_classes`; QUALITY weights | Derived after selection; restamp when CFG changes |
| `MachineEdge.source_block`, `destination_block`, `copy_offset`, `copy_count` | Selection, shared canonical-edge construction, edge splitter; verifier and parallel-copy placement | Function arena. Source/destination are machine block IDs; copy slice matches destination parameter order |
| `MachineEdge.flags`, `MachineBlockParameter.flags` | Zero-initialized structural replay slots | No active nonzero semantics; reserved compatibility slots, not authority |
| `MachineBlockParameter.virtual_register` | Selector and edge splitting; verifier/liveness/edge copies | Defined at block entry; changes require all incoming copy slices and value IDs to agree |
| `MachineLineMark.row`, `instruction` | Selector; scheduler remaps/sorts; debug emission | Borrows canonical source row identity. Reorder invalidates machine row; canonical row remap invalidates instruction ID |
| `MachineSwitchCase.value`, `target_block`, `compare_width` | Selector and block remapper; switch encoder, verifier and CFG walkers | Function arena. INDIRECT_BRANCH uses only target_block. Width zero preserves the documented 64-bit compatibility form; block changes remap targets |
| `MachineEdit.point`, `kind`, `flags`, `subject`, `location` | Allocators; ordered encoder edit stream | Placement arena. Kind selects virtual/physical/immediate/temp-offset/direct-frame interpretation. Invalidate after source rows, register file or frame layout change |
| `MachinePinSplitEntry.block`, `virtual_register`; `MachinePinSplitStore.row`, `virtual_register` | QUALITY candidate construction; pinned FAST placement | Temporary allocation attempt; source CFG/rows/value IDs and candidate pins must match |
| Reserved words in these records | Zero initialization and structural copies | Padding or explicitly reserved replay slots; no consumer may treat them as hints |

`MachineTargetDescription` is process-lifetime immutable data produced by
`machine_target_x86_64`, its Windows variant, and `machine_target_aarch64`.
The following table exhausts its current semantic fields. Target changes
require reselection and rebuilding all placement, scheduling and encoding
results; descriptions are not interchangeable merely because their files
have the same size.

| Fields | Consumers |
| --- | --- |
| `allocatable_mask`, `callee_saved_mask`, `register_count` | Verifier physical bounds, all placement modes, QUALITY pinning and encoder save areas |
| `slot_scratch`, `vector_slot_scratch` | MIR_STACK and constrained operand assignment |
| `vector_allocatable_mask`, `vector_register_mask` | Class validation, active register-file bounds, liveness and placement |
| `copy_opcode`, `vector_copy_opcode`, `constant_opcode` | Copy coalescing, literal rematerialization and emission |
| `indirect_call_opcode`, `indirect_call_register` | Call barriers and callee-pointer staging |
| `unconditional_branch_opcode`, `switch_opcode` | Edge normalization and cold-entry/edge-contract construction |
| `float_bridge_opcode`, `float_bridge_register` | Placement constraints around implicit vector bridges |
| `quality_pin_registers`, `quality_pin_register_count` | QUALITY candidate admission in stable target preference order |
| `saves_precede_frame_pointer` | Placement incoming offsets and encoder prologue/unwind ordering |
| `predicate_allocatable_mask` | Separate predicate placement admits only k1-k7 on supported x86 targets; zero disables the bank. The mask never expands the shared 48-register GPR/ZMM tile; k0 remains reserved |

## Derived analysis, placement and result records

The function-owned table above covers every `MachineFunction` field,
including its canonical provenance, storage, ABI, relocation and platform
side data. The following records are temporary consumer-specific results,
not additional authoritative IRs.

| Record / fields | Producer and consumer | Invalidation |
| --- | --- | --- |
| `MachineFastPrepass.rematerialize_immediates`, `definition_blocks`, `last_use`, `escapes`, `next_call` | Existing FAST prepass; FAST and QUALITY allocation | Exact source function rows, references, classes and CFG; build again after mutation |
| `operand_masks` | Same row walk; FAST and QUALITY operand classification | Same opcode/operand stream, no separate source of operand truth |
| `predecessor_offsets`, `predecessor_list`, `predecessor_edges`, `terminator_edges`, `cold_blocks` | Existing CFG prepass; edge conformance and pin plans | Same CFG, block layout and terminator slots |
| `interval_starts`, `interval_ends`, `disqualified`, `loop_spans`, `loop_span_count` | Built only when QUALITY requests facts; QUALITY candidate ranking | Same source function and constraints. Null/zero in FAST-only requests |
| `active_register_count`, `callee_saved_clobber_mask`, `valid` | Prepass; placement admission and saves | Same register classes, target and clobbers; invalid prepasses cannot yield valid placement |
| `MachineStackPlacement.edits`, `edit_count`, `operand_registers` | MIR_STACK/FAST/QUALITY; encoders | Exact row stream, allocation and frame; scheduling a different order requires fresh placement |
| `virtual_register_offsets`, `stack_slot_offsets`, `frame_size`, `edge_copy_temporary_offset`, `incoming_base`, `callee_saved_mask` | Placement; frame loads/stores, parallel copies, prologue/epilogue and unwind | Exact slot sizes/alignments, target, outgoing area and allocated registers |
| Placement reload/spill/copy/rematerialize/pin/split/boundary counters | Same allocation operations; QUALITY cost comparison and codegen diagnostics | Belong to that candidate, never mixed with another placement |
| Placement `valid` | Placement builder; codegen/encoder admission | Set only after complete frame/edit construction |
| `MachineSelectResult.function`, `failed_opcode`, `supported`, `returns_value`, `selector_certified`, `signature_rejected` | Native selector; codegen admission, fallback and diagnostics | Selected function arena, canonical inputs and target. Certification applies only to that output; manual/replayed functions require verification |
| Selection typed/machine/SIMD/mutable counts | Existing selector walk; codegen statistics | Attempt-local; preserve rejected-attempt accounting |
| `reserved_selection_layout` | Explicit cold padding | The 2026-08-17e experiment rejected removal; no dormant matcher or telemetry is retained |
| `MachineScheduleResult.function`, `moved` | Scheduler; QUALITY candidate evaluation | Shares unchanged source arrays and borrows input arena; new rows/points/line marks have the candidate's arena lifetime |
| `MachineEncodeResult.bytes`, `byte_count`, `block_offsets`, `row_offsets`, `valid` | Target encoder; module emission and debug mapping | Exact placement and row stream; false valid is not usable partial code |
| `frame_allocation_offset`, `epilog_offsets`, `epilog_count` | Native encoder; Windows unwind construction | Exact emitted prologue/epilogue instruction boundaries |
| `call_sites`, `call_site_count` | Encoder; module relocation creation | Function-relative emitted offsets and selected call-target pool |
| `exact_attempts`, `exact_successes`, `exact_failures` | x86 exact emission; codegen diagnostics including rejected attempts | Attempt-local, not a capability certificate |
| `MachineCallSite.code_offset`, `target`, `absolute`, `page_relative`, `page_low`, `is_thread_local`, `thread_local_low`, `thread_local_site` | Encoder marks exact fixup sites; module codegen selects format relocations | Encoded bytes and symbol-reference pool. TLS/page variants have distinct addend/width rules; never derive IDs from similarly named recipe values |
| `MachineVerifyResult.error`, `block`, `instruction`, `operand`, `mutable_virtual_register_count` | Verifier; diagnostics and tests | One verification attempt; later mutation invalidates success |

`MachineBuilderChunk.next/count` and `MachineBuilderStream.first/last`,
`total_count`, `element_size`, `chunk_capacity` are construction-only state.
Typed instruction/value/block cursor/end pairs defer chunk counts until
refill/finish; `MachineFunctionBuilder.arena`, its six streams,
`open_block`, `open_block_first_instruction`, `block_is_open` and
`point_capacity_exceeded` govern publication. Finish closes pending counts
and validates capacity. Never append after publication into an aliased
single-chunk result. The finished function retains only published arrays,
not a second mutable builder representation.

For stale derived data, the existing verifier independently checks defining
points against rows, block/edge extents and matching parameter/copy classes.
Scheduler candidates remap points and line marks, then receive fresh
placement. Raw replay establishes framing only and does not carry a trusted
selector certificate. Tests retain wrong-definition-point, reordered-storage,
invalid-edge, malformed replay, and new nonzero reserved-metadata controls.
These checks protect actual consumed relationships; no new permanent cache,
fingerprint, per-instruction generation field or speculative hazard model is
introduced.
