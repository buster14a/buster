# Machine instruction selection and scheduling

[Agent instructions](../../AGENTS.md) · Paths and commands below are relative to the repository root.

## Machine instruction selection and scheduling

- `MachineInstruction` is the 24-byte hot row. Keep static scheduling,
  memory-effect, fixed-register, tie, early-clobber, register-clobber, and
  implicit-vector-scratch membership in `MachineOpcodeInfo`, accessed through the
  `machine_opcode_*` helpers.
- Emission recipes come from the separate immutable recipe projection;
  exact x86 forms come from checked encoding metadata. The unused descriptor
  form-set and expansion fields/accessors are removed. Their reserved bytes
  only preserve layout. Dormant memory hints, timing, bundle state, duplicate
  recipe and implicit-physical masks are also removed; reserved bytes are
  not a policy seam. See the [identity joins](../machine-metadata-ownership.md#removed-unused-form-and-expansion-identities)
  before deferring a form choice past scheduling or placement.
- `MachineOpcodeInfo` retains a 96-byte stride. Operand and allocation
  constraints occupy its first 32 bytes; explicit barrier and implicit-vector
  membership are cold. Unused names and speculative resource/cost bits are removed. Keep opcode initializers designated and the
  layout checks intact. Simple FAST rows use the separate 16-byte
  `MachineOpcodeRow` projection instead of loading the full descriptor.
- Address expressions remain canonical IR / selector-owned. Both native
  selectors consume `machine_selection_address` from `machine_select.c` for
  field offsets, index scale/extension and transparent address bases. Its
  demand cache uses at most 4 KiB per selection attempt, with no function scan
  or per-value allocation; bounded chains/collisions remain conservative.
  Discard the cache whenever canonical IR changes. Exact expression ids retain
  subobject and computed-label identity; only LOCAL/GLOBAL roots name objects.
  Loads, atomics, casts and pointer/integer arithmetic stay opaque. Original
  symbol rows own TLS/GOT/relocations, and offset folding never modifies them.
  These facts authorize no memory reordering or dereference. No `MachineAddress`
  MIR side table is produced; keep `MACHINE_REF_ADDRESS` stable.
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
  General/vector physical references must fit `MACHINE_TARGET_REGISTER_LIMIT`
  and the active target's file; the separate x86 predicate IDs admit only k1-k7.
  `vector_register_mask` describes class membership including
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
  ordering across writes, and SSA-only consumers must reject mutable values.
- Canonical block parameters are defined by incoming edges, so their
  `IrValue.definition` is invalid by design. Selectors must still classify
  them as values and accept their pointer registers as address bases.
  Only instruction-defined locals take the frame-address path; an absent
  instruction definition is not a missing value.
- Native i128 block parameters expand to two general-register MIR parameters.
  The selector allocates pair mappings only for functions with wide joins and
  snapshots each incoming instruction result at its definition. Entry stores
  restore the parameter's frame representation for body operations; ordinary
  parallel edge copies preserve both limbs through loops and assignment cycles.
  Keep limb mappings separate from scalar value registers and leave parameter
  definition points invalid. `basic_c_i128_block_parameters.c` covers all six
  desktop targets, every allocator and both frontend forms with zero fallback.
- The verifier separates entry-reachable code from unreachable components.
  Unreachable source SCCs attach to a synthetic dominator root, with every
  member of a closed source cycle treated as an entry. Block storage order
  cannot create dominance, and a join reachable from multiple disconnected
  entries cannot inherit one entry's values without block parameters.
- The opcode switches in `machine_select_canonical_function_x86_64` and
  `machine_a64_select_instruction` are the authoritative machine selections.
  Add a selection to the target switch and its direct helpers, with MIR and
  generated-code regressions; do not add a parallel matcher that reports a
  rule without producing the selected MIR. `machine_select.{c,h}` owns only
  consumed type/value/address facts, row layout, and the unvalidated entry's shape
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
- Native signature and call storage is sized from canonical IR counts. Incoming
  shapes, placements, argument values and normalization rows use arena arrays;
  the existing value-fact walk sizes one reusable call workspace per function.
  x86-64's per-type signature pool sums parameter counts in 64 bits. Stack
  placements use 32-bit offsets and check the signed frame-displacement bound
  before advancing cursors. There is no fixed source argument-count limit.
  The many-argument fixture covers 25 integer, 33 floating, 65 narrow scalar,
  33 mixed aggregate/HFA parameters and a 65-value variadic tail, with direct
  and indirect calls and a hidden aggregate result pointer. A 522-parameter
  variadic signature checks the general incoming-address expansion past 4095
  bytes. Darwin callers extend narrow integer register arguments to 32 bits
  before physical-register staging, as required by its public ABI.
- x86 ADD/SUB/AND/OR/XOR/IMUL rows are three-operand machine SSA with operand
  0 tied to operand 1. Allocators satisfy the physical two-address constraint;
  selectors must not reintroduce a MOV plus mutable USE_DEFINE result.
- QUALITY scheduling remains pressure-first and deterministic. Pressure is
  counted per register class; metadata supplies barriers, memory membership,
  and implicit vector-state chain membership through the published
  `MachineOpcodeRow.schedule_flags` byte. There are no parallel scheduler
  opcode classifiers. The [metadata ownership inventory](../machine-metadata-ownership.md)
  documents every shared record's producer, consumer, publication and invalidation.
  Explicit barrier/vector membership is not a latency or hazard model.
- QUALITY placement accumulates exact u64 weighted traffic for values and loop
  regions, including split-boundary costs. A u32 edit count and maximum weight
  4096 bound a traffic sum below 2^44. The heap preserves its strict-greater tie
  policy; regional probes order by descending traffic then increasing region
  index. The first regional probe has no upper-cost sentinel. Raw edit counts,
  marginal eligibility and the first-4096 candidate policy remain unchanged.
  The private `register_allocator_quality_internal.h` helpers are shared with
  bounded arithmetic/ordering tests. The 24-byte interval, 8-byte traffic cells
  and corresponding diagnostic clear/copy accounting are checked together.
- Static memory-chain membership comes only from `MachineOpcodeInfo.memory_effect`
  through `machine_opcode_is_memory`; the duplicate memory attribute bit is
  removed. Calls, side effects and terminators still impose independent
  barriers. A missing memory effect is not permission to reorder a barrier.
- Memory scheduling uses explicit stack-range alias classes only when the
  selector certifies no volatile access in the function or the particular
  frame object. Mixed functions derive optional `stack_slot_memory_flags`
  before canonical-to-machine row spans are remapped; every frame operand in
  a volatile source span taints its entire object, including split accesses.
  The compact object certificates are immutable and survive CFG/SSA/schedule
  row changes. Unknown/manual/structural-replay functions default to the
  original all-memory chain; replay drops both certificate forms. A producer
  adding volatile accesses must clear `nonvolatile_memory_certified` and
  invalidate the affected object certificates. Invalid certificate bits are
  rejected by the machine verifier; invalid source spans publish no proof.
  Known scalar frame forms, x86 512-bit frame transfers, and scalar pointer
  operations with a checked immutable SSA `LEA_FRAME` definition qualify only
  after their slot id and byte range are checked. The volatile producer uses
  that same raw address proof to taint pointer-based accesses before publishing
  certificates. Mutable pointers, incoming/phi pointers and pointer arithmetic
  remain unknown. Each object owns one, two, four,
  or eight dependency cells covering eight-byte ranges. Larger objects fold
  cell indices modulo eight: collisions retain conservative ordering. Every
  overlapping access shares a dependency; disjoint fields can move. Unknown
  pointer, aggregate-copy, incoming-argument, and
  unrecognized memory rows flush all pending slot chains; calls, atomics,
  fences, and physical-register rows retain their full barriers. Explicit
  mutable-vreg touch ordering remains necessary for target-promotion fallback,
  including a mutable block parameter with only one textual update. Reads
  after a sole update may remain independent, while earlier reads precede
  that update. A definition count does not replace mutable classification.
  The dependency builder uses epoch-stamped slot tails and a compact pending
  list, with at most 23N+8 edges for N rows and source-order fallback before a
  scratch count can overflow. The FAST allocator does not build alias chains;
  only mixed volatile functions add the selector's optional certificate work.
- x86 predicate values use `MACHINE_REGISTER_CLASS_MASK` in a separate k1-k7
  bank; k0 is never allocatable or a valid explicit predicate operand. The
  GPR/ZMM register tile stays at 48 entries. `machine_x64_select_predicates`
  retains compare/copy/AND/OR/XOR chains in predicates for FAST/QUALITY and materializes the
  ordinary integer representation only for integer, storage, call or CFG-edge
  users. Byte predicates carry 64 bits; dword compares clear the upper 48 and
  return through KMOVW. Explicit mask moves support 8/16/32/64-bit truncation.
  Source bridges use 16/64-bit forms under the SIMD feature gate; synthetic
  8-bit KMOVB rows also require AVX-512DQ on the executing CPU. MIR targets
  describe the ABI/register file, not per-function CPU feature permissions.
  Mixed integer constants and full-width C integer complement retain scalar
  bridges; their semantics do not become a narrower predicate complement.
- `register_allocator_predicate.c` projects MASK operands out of ordinary
  FAST/QUALITY placement, then places the tiny bank separately. Call-clobbered
  predicates and escaping block values reach eight-byte homes. Predicate edge
  copies capture all outgoing sources before publishing any destination, with
  fixed physical sources captured before reload scratch can overwrite them.
  Unused predicate homes are removed, and zero/all-ones integer bridge values
  rematerialize without a memory reload. Explicit MASK MIR is supported in
  MIR_STACK, where it flushes after each row. Source MIR_STACK selection keeps
  its existing integer bridges because it cannot retain K values between rows;
  the selector's `predicate_residency` argument records that allocator policy.
  `predicate_absence_certified` skips this discovery for fresh scalar functions;
  a rewrite adding MASK references must clear that proof. Replay defaults to
  discovery. Predicate pressure has its own seven-register scheduler budget.
  `basic_c_predicate_bank.c` and the machine module cover source selection,
  independent residency, spills, calls, widths, fixed operands and edge cycles.
- SysV x86-64 f80 values use sixteen-byte frame homes. `machine_x64_emit_f80`
  emits closed metadata-backed x87 transactions, with one explicit ST(i)
  operand and architectural ST(0) left implicit in the exact token. Arithmetic,
  negation, comparison and conversion rows carry memory/barrier membership;
  no x87 register class is allocated. Comparisons repair unordered flags,
  integer casts save/restore the caller's control word, and only call/return
  bridges carry live ST results. Frame sizes and operation payloads are checked
  by the MIR verifier. Scalar loads/stores copy ten payload bytes, while
  constants and computed results clear private padding. Two-limb edge copies
  carry f80 joins; ABI transport includes single-f80 wrappers and complex
  results. See [wide float boundaries](frontend/wide-floats-assembly.md) for
  unsupported i128 casts. Unsigned-64 conversion now composes signed casts
  with scalar masks and exact zero/2^63 f80 corrections, without new opcodes
  or CFG blocks. The extended-precision contract retains all 64 integer bits;
  arbitrary rounding-control modes and the complete control word are preserved.
- System V x86-64 machine callers retain the sixteen-aligned push area for
  tightly packed arguments. A padding gap or greater base alignment selects
  a saved-RSP SSA value and an ordinary `STACK_ALLOCATE` row for the complete
  outgoing area. Copies use the classified argument offsets; fixed parameters
  and anonymous variadic tails share the same rule. Capture call results before
  restoring RSP so a reload of the saved value cannot overwrite RAX/RDX/XMM0.
  This preserves any live alloca and permits repeated calls without leaking
  stack space. The registered over-aligned stack fixtures require strict
  scalar MIR objects on ELF/Mach-O and execute mixed host/Buster calls in both
  directions on supported ELF hosts; the vector module also requires its
  ninth-ZMM caller to select and encode without fallback.
- Instruction-cache clearing preserves argument side effects on every target.
  The x86 selector emits no cache operation. AArch64 selects one constrained
  barrier row with begin/end in X9/X10, X9/X11 clobbered, and NZCV defined. Its
  data-clean and instruction-invalidate walks cover aligned four-byte granules
  through the exclusive end, with DSB/ISB barriers. The direct AArch64 oracle
  uses the same alignment rule; an unaligned start must not skip a final line.
- Sixteen-byte AArch64 atomic loads and stores select constrained pair rows.
  A load uses LDXP/LDAXP, writes the observed halves back with STXP (STLXP for
  sequential consistency), and retries until the read is single-copy atomic.
  A store stages both halves, clears any promoted aggregate padding in the high
  half, arms the monitor with LDXP or LDAXP, and retries STXP/STLXP until the
  replacement lands whole. The rows
  preserve the direct emitter's memory-order strengths on every desktop ABI.
  Sixteen-byte exchange, arithmetic/bitwise RMW and compare-exchange use
  constrained update rows with full-width integer input/result frame slots.
  RMW reloads its unchanged operand on each retry and propagates carry/borrow
  across both limbs. CAS compares both halves, selects the observed pair on
  mismatch, and still completes STXP/STLXP before returning: an unvalidated
  LDXP may be a torn read. The direct oracle follows the same corrected rule.
  Every frame load precedes LDXP on each retry; only register operations
  occur before STXP, preserving the exclusive-loop progress guarantee.
  Both rows declare X9/X11-X14 clobbers and flag definitions; CAS additionally
  stages desired in X15/X17. X10 is the constrained address, and X16 remains
  reserved frame scratch.
  `basic_c_aarch64_atomic_update_pair.c` checks both-limb results, boundary
  arithmetic, strong/weak CAS, promoted padding and a large native frame.
  The machine tests independently check complete retry windows and invalid
  payload/frame contracts; the mnemonic oracle is
  `tests/aarch64_atomic_update_pair_oracle.s`.
- Windows/UEFI x86-64 variadic definitions home RCX/RDX/R8/R9 before any
  argument capture can reuse those registers. The caller-owned homes adjoin
  the overflow arguments; both homing and `LEA_INCOMING` include placement's
  `incoming_base` for callee-save pushes preceding RBP. Each supported named
  parameter consumes one slot, and a hidden return pointer consumes the first.
  Pointer-sized `va_list` copies use eight bytes and `va_end` emits no write.
  Scalar and aggregate `va_arg` reads advance one slot, dereferencing indirect
  aggregates and 128-bit integers according to the canonical ABI classification.
  Indirect reads copy the complete value, including aggregates above sixteen
  bytes. On AVX-512 targets, sixty-four-byte vectors use the same cursor and
  indirect load, including named vector parameters and copied lists.
  Variadic callers duplicate scalar float bits into positional GPRs during the
  integer staging pass, after all XMM bridges, and omit the System V AL count.
  Cross-compiler regressions cover both call directions, register exhaustion,
  copied lists, small/indirect aggregates, and hidden result pointers.
- Windows/UEFI x86-64 indirect aggregate arguments occupy one pointer slot.
  Callers copy exact value bytes after their shadow and stack-argument area.
  Copies meet both the sixteen-byte floor and the declared type alignment;
  stronger alignments reserve bounded slack and round the copy pointer through
  ordinary MIR address arithmetic. Fixed frames reuse the maximum outgoing
  reservation, while calls below a live dynamic allocation reserve their own
  area. Fixed and variadic arguments share this ABI placement. Callees
  capture incoming register pointers before floating bridges, capture stack
  pointers next, and materialize parameter objects after all incoming captures.
  Parameter writes affect the private value copy. Complete outgoing sizes are
  checked before narrowing frame displacements. Cross-compiler tests cover odd
  sizes, larger aggregates, hidden returns, indirect calls, large anonymous
  arguments, mixed floating parameters and caller-value preservation. The
  `win64_aligned.c` regression exercises 32/64/128-byte argument alignments
  across host/MIR boundaries, including raw variadic argument pointers and
  every sixteen-byte dynamic-stack residue modulo 128. Fixed calls also execute
  through the direct backend; wide variadic reads use all three MIR allocators.
- Windows/UEFI x86-64 128-bit integer arguments use the same one-pointer
  placement and sixteen-aligned private copies as indirect aggregates. A
  128-bit integer result travels whole in XMM0, without a hidden result
  pointer. The frame transfer rows use metadata-selected MOVDQU, publish
  their memory and implicit vector effects, and verify sixteen-byte storage.
  The registered `tests/differential/win64_wide.c` fixture covers all MIR
  allocators and both frontend forms. On x86-64 Clang hosts, `ms_abi` JIT
  calls cross the host/MIR boundary in both directions without a PE loader;
  they cover register and stack arguments, indirect calls, copied lists,
  high limbs, private parameter writes, and forty-byte variadic aggregates.
  The direct backend still lacks wide variadic reads, so this fixture's
  complete module is a MIR gate rather than a NONE differential gate.
- Windows/UEFI x86-64 sixty-four-byte vector signatures use the existing ZMM
  vocabulary on AVX-512 targets. Arguments occupy one pointer slot with an
  aligned private copy; callers stage register values through a frame slot,
  and callees load whole vectors after all incoming pointer captures. Results
  return in ZMM0. Vector literals construct their complete lanes into a
  temporary frame before loading the resulting vector. Fixed and variadic
  calls share the pointer placement, including named vector parameters.
  `win64_vector.c` crosses Clang/MIR boundaries in both directions on capable
  hosts, checks every result lane, caller preservation, raw variadic pointer
  alignment, and live dynamic allocations. The fixed subset also executes
  through NONE. Smaller vectors and model-dependent register splitting retain
  the direct fallback; this change does not replace their representation.
- Windows/UEFI x86-64 MIR frames larger than one page reuse
  `codegen_x64_emit_windows_stack_allocate`, the direct emitter's bounded
  R10/R11 probe loop. RSP stays unchanged until the final allocation, so a
  large frame requires one allocation unwind action and a bounded prologue.
  `MachineEncodeResult.frame_allocation_offset` supplies its actual byte offset
  to unwind construction without widening the result on 64-bit hosts.
  Keep object creation and native Windows execution of
  `tests/basic_c_win64_large_frame.c` covered in every allocator mode; page
  probing must preserve all incoming argument registers and private copies.
- AArch64 vector bodies use NEON when an exact form exists and otherwise
  expand into scalar MIR lanes before allocation. Exact-width lane loads
  preserve signed narrow operands; comparisons produce all-ones true lanes,
  and floating negation toggles the sign bit. Division, remainder, shifts,
  comparisons, 64-bit-lane multiplication and vector unary operations share
  the scalar arithmetic rules. Keep the entire vector fixture strict in
  MIR_STACK, FAST and QUALITY; scalar expansion must remain visible to MIR
  validation and register allocation.
- AArch64 128-bit multiplication combines the low-limb product, its generated
  UMULH high half, and the two cross products. Negation propagates the low
  limb's borrow. Variable shifts use masks at the 64-bit boundary and suppress
  the cross term at count zero. These are scalar MIR rows, with synthesized
  registers created at their actual defining row; creating several registers
  ahead of their definitions publishes incorrect verifier metadata.
  `basic_c_x86_64_i128_binary.c` and `basic_c_i128_shift_edges.c` require strict
  MIR selection and cover product carries and counts below, at and above 64.
- Native signed/unsigned i128 division and remainder use a bounded restoring
  loop over ordinary scalar MIR. Five block parameters carry the evolving
  quotient/dividend, remainder and bit count; parallel edge copies keep the
  loop in SSA. Signed magnitudes and results use explicit low-limb borrow.
  The selector counts splits during its existing value-fact walk and allocates
  canonical-to-machine entry/exit maps only for functions containing a wide
  divide. Remap branch, switch, label-address and indirect-branch targets to
  entries, and canonical outgoing edges from exits; preserve original block
  parameters on the entry. The registered division fixture checks exact results
  against an independent scalar-limb reference and exercises surrounding CFG
  edges. It and the unchanged wide-integer fixture require zero fallback on
  all desktop AArch64 and all six x86-64 targets and all MIR allocators with
  both frontend forms. x86-64 uses the existing constrained scalar shift rows;
  variable wide shifts select straight-line limb masks with the cross term
  disabled at count zero, and negation propagates the low-limb borrow. The
  independent shift-edge fixture checks counts across the 64-bit boundary.
  A separate direct-oracle fixture retains zero-divisor and signed-overflow
  bit results; those inputs are excluded from Clang differential checks.
- Native Windows TLS addresses read the module index and the TEB's TLS array,
  then add the object's thread offset. Constrained rows define RAX/X9 and
  declare RDX/X10 scratch clobbers. Darwin TLS descriptor rows have ordinary
  call effects; a following move captures RAX/X0 into an SSA value. Every
  relocation site distinguishes the index, value offset, or descriptor field.
  The thread-local model fixture requires zero fallback with all desktop
  targets, allocators, frontend forms, and PIC settings; native hosts execute
  its separate definition object and values held across repeated TLS accesses.
- x86-64 i128 bitwise complement reads both frame-backed limbs and emits
  ordinary three-operand XOR64 rows against one all-ones constant. Each limb
  result has one definition; do not use mutable NOT rows for this expansion.
  `basic_c_x86_64_i128_complement.c` checks signed/unsigned loaded values,
  every bit position and in-place stores in all modes and both frontend forms.
- AArch64 f32/f64-to-i128 casts use scalar MIR conversions and arithmetic.
  Widen f32 before splitting the absolute magnitude at 2^64, convert both
  unsigned limbs with truncation toward zero, and restore signed results
  with an explicit low-limb borrow. Both destination limbs are overwritten.
  The registered finite-input fixture decodes IEEE images with integer
  operations and requires strict compilation across all desktop AArch64
  targets, all MIR allocators, and both frontend forms; native hosts execute
  the same cases, retaining NONE as the direct reference.
- AArch64 leading/trailing-zero counts use importer-generated CLZ and RBIT
  forms for ordinary 32/64-bit scalar rows. A 128-bit count operates on both
  slot-backed limbs, selecting the primary limb's count or 64 plus the other
  count with ordinary scalar MIR. Publish a zero high result limb, including
  the direct oracle's all-zero-pair result of 128. Never truncate the operand
  to a single limb or leave stale high result bytes. Preserve existing replay
  opcode numbers by appending new rows. The registered zero-count fixture
  covers every one-bit position and both frontend forms, with strict MIR
  object checks on all three desktop AArch64 targets and native-host execution.
  Further i128 coverage is tracked in [#69](https://github.com/buster14a/buster/issues/69).
- ELF/Mach-O AArch64 fixed frames are not limited by the scaled callee-save offset.
  Above that offset's reach, the prologue and each epilogue derive the compact
  save-area base from X29 in reserved X16, then use small unsigned offsets.
  Module unwind construction counts both setup words and retains the actual
  SP-relative save locations. Capacity planning includes large-offset body
  transfers and aggregate-copy pieces; frame-size sums are checked before
  narrowing. Keep strict large-frame and packed-layout tests in all MIR modes.
- AArch64 calls in functions containing dynamic stack allocations reserve
  stack arguments below the current SP. The existing canonical row walk
  records this property before selecting any call, including calls before
  the first allocation. Ordinary functions retain one reusable fixed area.
  Dynamic calls use explicit `READ_SP`, `STACK_ALLOCATE`, pointer stores and
  `WRITE_SP` rows; the saved SP stays live across call clobbers and result
  capture. Packed Darwin argument widths and aggregate stack images retain
  the shared ABI placement. No outgoing area is hidden in an emitter pseudo.
  `basic_c_aarch64_dynamic_calls.c` covers repeated indirect calls, calls on
  both sides of a VLA, packed narrow arguments, split pairs, indirect large
  results, ninth floating arguments and variadics. The registered driver
  matrix retains both original over-aligned stack fixtures, all six AArch64
  targets, allocator modes, frontend forms and PIC settings. Native AArch64
  desktop hosts also link the independent host observer in both directions.
- Win64 x86-64 dynamic frames establish RBP at the bottom of the fixed
  allocation, after the probe, so PE unwind records retain SET_FPREG. Encoding
  rebases logical frame offsets once at the existing exact/fast memory paths.
  Each call reserves shadow space, stack arguments and private aggregate copies
  below the live VLA and releases that area afterward. A frame-relative LEA
  restores RSP for every return; `basic_c_win64_dynamic_stack.c` covers nested
  allocations, page crossings, indirect calls and aggregate argument copies.
- Windows/UEFI AArch64 MIR saves FP/LR, allocator-owned X19-X27, and X28 in
  a compact, sixteen-aligned prefix before establishing X29. Fixed body
  slots remain X28-relative; incoming stack arguments are relative to X29
  plus the complete prefix size. The shared bounded Windows probe leaves
  SP unchanged until its final allocation. Unwind actions describe every
  prologue instruction, and every epilogue shares the unwind suffix starting
  at SET_FP: restore SP from X29, reload X28 and allocator saves in reverse
  order, then restore FP/LR and release the prefix. This also discards VLA
  allocations. The native `windows_arm64_mir_unwind.c` test derives synthetic
  boundary contexts from instruction effects and checks RtlVirtualUnwind's
  restored registers; object-only checks are not native unwind acceptance.
  Windows variadic definitions extend the prefix with a 64-byte X0-X7 image
  ending at the incoming SP. A constrained entry row homes the registers
  before parameter captures. Ordinary cursor arithmetic then crosses from
  register homes to caller stack arguments, including lists consumed in a
  separately compiled function. The allocation action includes the complete
  prefix and the native unwind fixture covers a variadic frame.
- Windows AArch64 variadic callers and definitions use the integer argument
  image for named and anonymous values, including floating-point bits and
  homogeneous aggregates. A composite may span X7 and the incoming stack;
  sixteen-aligned integer pairs skip odd slots. Darwin keeps independent
  register files for named arguments, packs named stack arguments at their
  natural alignment, and places anonymous arguments in aligned stack slots.
  Both platforms use public eight-byte pointer lists. `va_copy` copies eight
  bytes; `va_arg` selects ordinary MIR loads, alignment, cursor stores and
  exact aggregate copies for the supported values through sixteen bytes.
  `compiler_driver_test_aarch64_platform_variadic` requires zero fallback
  for every MIR allocator and frontend form, then executes native standalone
  and mixed-compiler callers, callees and public-list consumers on matching
  desktop hosts. ELF wrapper execution of target object code is useful ABI
  evidence but does not replace native loading or Windows unwind checks.
- ELF AArch64 variadic definitions capture X0-X7 and Q0-Q7 into a 192-byte
  save area before argument capture. The public 32-byte, eight-aligned list
  holds `__stack`, `__gr_top`, `__vr_top`, then independent signed 32-bit
  `__gr_offs` and `__vr_offs`. Initial offsets are minus the remaining save
  bytes. Read a register argument from its top plus its negative offset;
  update only that file's four-byte cursor. A nonnegative cursor or an
  argument that cannot fit uses the shared overflow pointer. Integer pairs
  requiring sixteen-byte alignment round both the GP cursor and stack address.
  Named aggregates that cannot fit close their register file before smaller
  following arguments. `va_copy` copies all 32 bytes; `va_end` emits no write.
  Lists passed by value use the existing AAPCS64 indirect aggregate argument
  plan and a private callee copy, not the original producer's cursor.
  The direct oracle and MIR also reconstruct three/four-double HFAs,
  64/128-bit short vectors and up to four-vector HVAs from independent V
  slots. An ordinary composite above sixteen bytes consumes one GP pointer
  and copies exactly the object size, including a short tail. Its pointed-to
  alignment does not align that pointer's GP/stack slot. Direct floating/vector
  stack arguments round to at most sixteen-byte alignment. Short-vector
  homogeneity compares vector width, independent of lane type, as specified
  by [AAPCS64 5.10.5](https://github.com/ARM-software/abi-aa/blob/main/aapcs64/aapcs64.rst#homogeneous-aggregates).
  HFA/HVA ABI edges transfer directly between frame slots and V registers:
  creating GP bounce values after integer argument staging could overwrite
  an already staged X argument in FAST/QUALITY. The native variadic fixture
  exchanges public lists and actual anonymous arguments with the host compiler
  in both directions, including larger HFAs, odd-sized and over-aligned indirect
  objects, mixed-lane HVAs, copied cursors and GP/FP exhaustion. Wider vectors,
  quad-precision floats and platform acceptance gates are separate from this
  supported read subset. Windows/Darwin pointer-list conventions are unchanged.
- Empty inline assembly with no operands or targets accepts an empty clobber
  list or any combination of `memory` and `cc` on x86-64 and AArch64. Both
  selectors consume `machine_selection_is_compiler_barrier` and emit their
  existing zero-byte compiler-barrier row. Its static metadata conservatively
  imposes a scheduler/memory barrier and defines condition codes even when the
  source omits either clobber; it is not a hardware memory fence. Other
  templates and physical register clobbers remain outside this #70 slice,
  apart from the operand transport, literal branches/hints and fixed CPU-query
  forms below. The strict driver corpus
  covers the accepted forms; selector tests also require explicit rejection of
  each unsupported class on both targets and in both frontend forms.
- Empty generic `r` operands select zero-byte `ASM_IDENTITY` rows with a real
  tied definition/use pair. Scalar integer, boolean and pointer inputs,
  read/write outputs, and numeric/named matching inputs transport 1/2/4/8-byte
  values through allocation. Every input is captured before outputs are
  stored, so swaps and overlapping places preserve simultaneous asm semantics.
  Pure outputs require one matching input; fixed registers, memory/vector
  constraints and more than 16 operands remain outside this subset. The strict fixture checks ties, swaps, input side effects,
  pointers and adjacent byte/halfword/word/doubleword sentinels in both forms.
- Literal AArch64 `b %lN` / `b %l[name]` and x86 `jmp %lN` /
  `jmp %l[name]` asm-goto, plus empty-template fallthrough, reuse the same
  generic-register transport before an ordinary `B`/`JMP` terminator. The shared canonical resolver accounts for read/write
  operands in GNU numeric label positions. Selection keeps only the executed
  MIR edge and that destination's canonical argument copies through the
  shared `machine_selection_finish_canonical_edges`; it also remaps AArch64
  edges through blocks split by wide division. This prevents unreachable asm
  destinations from assigning other joins during allocation. The strict
  six-target corpus requires zero fallbacks for these forms; MIR tests
  inspect the actual branch/edge pair on both architectures and runtime checks
  cover named/numeric labels, swapped outputs, fallthrough, joins, loops and
  a branch after an i128 divide splits the source block.
- Operand-free literal `nop` and x86 `pause` / AArch64 `yield` select distinct
  MIR rows and emit their architectural instructions. They accept the same
  `memory`/`cc` clobber contract as empty barriers, retain scheduling/memory
  effects, and expose no physical-register clobbers. x86 emission uses checked
  instruction metadata; PAUSE preserves the direct emitter's baseline policy.
  This also keeps `_mm_pause` and `__builtin_ia32_pause` in the machine path.
  Selector tests check instruction bytes at MIR row offsets, and the strict
  six-target driver corpus covers both frontend forms and every MIR allocator.
- x86 CPUID/XGETBV literal assembly with complete 32-bit pure outputs and
  separate fixed inputs selects constrained machine rows. Numeric/named ties
  retain the input's fixed register. CPUID consumes RAX/RCX together and
  clobbers RAX/RBX/RCX/RDX, so placement preserves RBX and resolves input moves
  in parallel. Each row snapshots zero-extended results to a private frame
  object before ordinary stores publish output places. XGETBV requires XSAVE
  on the compile target. Partial-width, read/write, partial-output and other
  assembly shapes retain their existing fallback; these rows do not implement
  unrestricted inline assembly.
- The x86 exact-emission bridge represents a full-width 32-bit immediate as
  its signed low-32-bit pattern. Normalize only when both register and
  immediate widths are 32; narrower immediates and 64-bit destinations retain
  their sign-extension constraints. High-bit unsigned switch constants must
  encode without canonical fallback.
- The f32/f64-to-u64 biased conversions compare against **2^63 in the source
  format**. Both x86 emitters use `CODEGEN_F32_SIGNED64_LIMIT_BITS` and
  `CODEGEN_F64_SIGNED64_LIMIT_BITS`; the source width does not change which
  integer bit the final bias restores.
- AArch64 symbol addresses on macOS/iOS use ADRP/ADD with Mach-O PAGE21 and
  PAGEOFF12 relocations in every allocator. The selector records the page
  reference beside the call target, and the encoder publishes both instruction
  sites. Absolute inline pointer literals in executable text are rejected by
  Apple's linker. Direct calls retain CALL26; ELF/PE address and TLS forms
  retain their existing target contracts. The qualified-aggregate differential
  corpus checks native Apple linking and execution across allocator modes.
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

## Wide integer conversion rounding

- AArch64 i128-to-f32/f64 casts normalize the magnitude as two scalar MIR
  limbs and retain round/sticky/parity bits before one nearest-even rounding
  at the destination precision. A zero high limb selects the ordinary u64
  conversion; a nonzero high limb scales the rounded significand by an exact
  power of two. Suppress the modulo-64 cross shift when CLZ(high) is zero.
  Signed results restore the floating sign only after forming the magnitude.
  The registered fixture constructs expected IEEE images in integer code,
  checks even/odd ties and significand carries across the limb boundary, and
  requires all desktop AArch64 targets and allocator/frontend combinations.

## Incoming argument reads

Both `MACHINE_X64_LOAD_INCOMING` and `MACHINE_A64_LOAD_INCOMING` declare
`MACHINE_MEMORY_EFFECT_READ`. Their implicit frame-relative address has no
fixed stack-slot identity, so the scheduler treats them as unknown memory and
chains them against every pending slot access. No architecture-specific
scheduler exception is needed. The stack-alias tests explicitly recognize
incoming reads independently of that metadata; otherwise a missing descriptor
bit could disappear from both the scheduler and its test oracle.
