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
| Operand effects | Active descriptor roles, shapes, ties, fixed-slot bindings and clobbers; instruction operands supply dynamic references | Row facts and allocator placement are derived. Unused resource-mask bits are not a second validated hazard model. `fixed_register_mask` and `fixed_registers` now exclusively describe explicit fixed assignments. |
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
| `name` | Dormant populated diagnostic name | Static string initializer; no in-process consumer in the audited tree |
| `operand_count`, `operand_info[4]` | Authoritative inline operand count, role, class and shape; verifier, scheduler, allocators; count and roles are projected into row facts | The target row defines the machine operation, including expansion scratch constraints. Each operand byte uses two role bits, three class bits and three shape bits; the new float-move helpers inherit register shape through the shared operand constants |
| `tied_pair` | Authoritative destination/source tie; MIR_STACK and FAST placement, verifier/tests | Two-address SSA rows; changing an opcode requires using its own tie |
| `early_clobber_mask` | Live constraint input, currently zero in every descriptor; FAST/QUALITY constraint detection | No current nonzero producer; the uncalled per-slot accessor has been removed |
| `fixed_register_mask`, `fixed_registers[4]` | Authoritative explicit physical operand assignments; verifier and all machine placement modes | A mask bit controls whether the corresponding register byte is meaningful; target-specific scratch fallback remains for other rows |
| `attributes` | Authoritative call, terminator, flags, constraint and rematerialization facts; verifier, scheduler and placement | The redundant `MEMORY` bit is removed; `BUNDLE`/`EXPANDS` do not currently drive bundle/expansion consumers |
| `clobber_mask` | Authoritative extra physical-register clobbers; all placement modes and compact row projection | Encoder-sequence scratch beyond explicit operands; also determines required callee saves |
| `schedule_class` | Authoritative scheduler barrier/vector membership where specified; published into `schedule_flags` | ALU/SHIFT/MUL/DIV/LOAD/STORE labels are not currently latency or throughput inputs; no scheduling cost model consumes them |
| `memory_effect` | Sole static conservative memory-chain classification; `machine_opcode_is_memory`, row publication | Includes six aggregate copies and the x86 incoming read formerly described only by a scheduler switch. It is not a complete hardware-memory-effects model: existing side-effect barriers can omit it |
| `implicit_resource_uses`, `implicit_resource_defs` | `VECTOR_STATE` is authoritative implicit vector-state chain membership; row publication | Float bridges, scalar conversions and AArch64 implicit V-register rows. Existing FLAGS/NZCV bits still have no resource-mask consumer; flag ordering currently uses `attributes` |
| `reserved_constraints`, `reserved_hot`, `reserved_form`, `reserved_expansion`, `reserved_metadata`, `reserved_recipe[4]`, `reserved_schedule[9]`, `reserved_physical[2]` | Padding, not semantic state | Zero; preserve the descriptor layout and must not be consumed |

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
terminator attributes and barrier/call/atomic schedule classes still impose
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

The live `early_clobber_mask`, fixed-slot assignments, `clobber_mask`, memory
effects, resource masks and recipe projection remain intact. The machine
suite checks the clobber projection across the whole opcode domain and the
specific RDX and RCX/ZMM0/ZMM1 scratch contracts independently of the removed
duplicates. Existing memory-chain, vector-state, constraint, registry and
encoding tests continue to cover their consumers. Reserved bytes preserve
the 96-byte descriptor and every retained offset; the 24-byte instruction
and 16-byte row projection are unchanged. There is no static-table byte
saving, new allocation or pass. Compiler-build and runtime evidence belongs
to the accompanying performance audit and exact-revision validation results;
removing declarations alone does not establish a speedup.

This is a bounded cleanup. The diagnostic name, unused attribute/resource
bits and metadata outside this inventory still need
an audit before #45 can close.

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
96/24/16 bytes. Compile-time checks also freeze the active `schedule_class`
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
inferring a speedup from removing unused declarations. Other dormant fields
in the inventory remain a separate cleanup; this slice does not close #45.

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
| `schedule_flags` | Descriptor barrier/memory predicates and implicit vector-state masks | Scheduler unit construction; same boundary |
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

Scheduler membership authority and removal of its three legacy classifiers
landed in #270. The fixed-register set is removed by the A12 slice above.
PR #256 already removed `MachineAddress`, `MachineSegment`, `MachineUse` and
`MachineLocationSegment`; their encoded reference tags remain stable. These
completed slices do not close #45:

- Audit the remaining diagnostic `name` and unused attribute/resource bits
  in a separate change. The nine dormant fields and three uncalled helpers
  described above are removed with the descriptor layout preserved.
- Audit unused FLAGS/NZCV resource bits against the actual flag-ordering
  attributes; populated masks alone are not an implemented hazard model.
  The duplicate memory bit is removed by the whole-domain comparison above.
- Extend the field census beyond the scopes above before claiming that all
  retained machine metadata is active. For example, virtual-register
  `rematerialization_recipe` and `hint` have no nonzero production producer
  in the checked tree, but `bootstrap_trace.c` reads them as diagnostic state.
  `typed_origin` is selector-written provenance consumed by selector tests,
  raw replay and bootstrap tracing. Document those diagnostic/serialization
  contracts and their invalidation before deciding whether to remove fields;
  they are not allocator rematerialization or register-hint authorities.
