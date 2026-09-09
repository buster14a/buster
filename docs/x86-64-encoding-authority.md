# x86-64 encoding authority and the first closed migration

Audit base: `9834a4253c61934a8a253641a84741a25d3717e3`, 2026-09-07.
This is an inventory of the production native encoding paths at that revision,
a design contract for subsequent migrations, and the TLS migration's boundary.
It is **not** a claim that all parser tables, extension forms, or scheduling
models have already been unified.

## 1. What already owns ordinary instruction bytes

Keep `src/buster/lib/compiler/assembly/x86_64_metadata.{c,h}` as the authority.
Its pointer-free generated XED snapshot, normalized forms, operand bindings,
checked physical requests, and exact-form tokens are the existing foundation.
Do not introduce another opcode database or another general-purpose encoder.

The generic packing kernel is `buster_x86_metadata_emit_form_to_scratch`.
`buster_x86_metadata_emit_machine_fast` has scalar and template specializations;
these are derived plans, not permission to maintain another ISA description.
Public routes include `buster_x86_metadata_encode`, `emit_form`,
`emit_form_exact`, `emit_form_selected`, `emit_form_key`, `emit_exact`,
`emit_exact_query`, `emit_exact_prevalidated`, and `emit_exact_machine`, all
with the `buster_x86_metadata_` prefix. Counting wrappers as independent
encoders exaggerates duplication; counting only the wrappers misses escapes.

### Production consumer inventory

Paths below are relative to `src/buster/lib/` unless stated otherwise.

| Consumer | Route and independent decisions still present |
|---|---|
| `compiler/assembly/assembly.c` | Intel/AT&T parsing and inline/global assembly become physical operands, then `assembly_x86_metadata_select_source_form` / `assembly_x86_metadata_emit`. Final ordinary bytes use metadata. |
| The same assembler's size/legality paths | `assembly_x86_memory_displacement_size`, `memory_encoding_size`, `instruction_size`, `general_instruction_size`, `size_*`, `evex_*`, `apx_*`, `amd_*`, `amx_*`, and `mask_instruction_size` still duplicate size, immediate, address, suffix, feature and operand decisions. AMD/vector form tables are not another final byte packer, but they are independent encoding-decision authorities that must eventually become projections. |
| `compiler/codegen/codegen.c` | `codegen_canonical_x64_metadata_emit*` and relocation helpers adapt canonical lowering to metadata. Query, immediate/displacement, and byte-template caches are derived emission routes. Scalar/SIMD/x87/EVEX helpers and ABI expansion choose operations; they must not invent fields. Inline/global assembly rejoins the source assembler. |
| `compiler/codegen/machine.c`, `machine_x86_64.c` | Exact DIRECT/FAMILY recipes, shape caches, prevalidated register/memory/immediate templates, and EXPANSION switch. `machine_x86_64_exact_prewarm` prepares exact shapes. The registry has 126 rows: 47 DIRECT, 50 FAMILY, 29 EXPANSION, plus documented LEA_BLOCK, INDIRECT_BRANCH and LOAD_SYMBOL_GOT surfaces. These are dispatch/expansion paths, not 126 encoders. |
| `x86_64.c` | `x86_64_encode_register_operation` already sends ordinary register operations through metadata, including extended-register variants. CPU-identification constants are not instruction emission. |
| `compiler/jit/jit.c` | `jit_emit_thunks` encodes the indirect JMP through metadata; its embedded target address is data. |
| `compiler/link/link.c` | `link_x86_emit`, `link_x86_emit_push_imm32`, ELF/PE startup, import/PLT stubs, and Mach-O destructor runners generally use metadata. Object-format and ABI policy remain here. Exceptions are enumerated below. |
| Field writers in assembler/codegen/object/link/JIT | Bounds-checked little-endian immediate/displacement/relocation writes are legitimate consumers of field descriptors. A writer that changes opcodes or reinterprets register bits is an encoder/relaxer, not a neutral patcher. |

### Eight raw construction, rewriting, or padding sites

The audit followed output writers, literal arrays and opcode substitutions,
not just function names containing `encode`.

| Site at the audit base | Disposition in this change |
|---|---|
| `codegen.c:codegen_canonical_x64_thread_local_general_dynamic` | Migrated: raw 16-byte TLSGD sequence becomes a metadata-owned recipe. |
| `machine_x86_64.c:MACHINE_X64_TLS_GENERAL_DYNAMIC`, via `machine_x64_emit_literal_bytes` | Migrated: same recipe; literal helper removed. |
| `link.c:link_elf_relax_thread_local`, general-dynamic arm | Migrated: metadata-derived FS MOV + fixed-displacement LEA replacement. |
| The same function, initial-exec arm | Migrated: metadata-derived ADD input/output forms, not manual REX/ModRM surgery. |
| `link.c:link_forwarding_runtime_object` | Migrated in the [forwarding follow-up](x86-64-forwarding-authority.md): metadata-prepared XOR/JMP recipes; object ABI policy stays in the linker. |
| `link.c:link_x86_relax_got_load` | Migrated: bounded section adapter; metadata derives and validates the MOV/LEA pair and its PC32 field. Broader GOTPCRELX conversions remain #78. |
| `assembly_unit.c:assembly_unit_directive_align` | Migrated: shared target-aware derived padding. Explicit source fill stays data; #228 partial AArch64-word policy is preserved. |
| `codegen.c:codegen_generate_canonical_module_attempt` | Migrated: same shared padding helper; x86 NOP is derived once and bulk-filled with one memset per gap. |

The existing five-file writer census does not discover every literal array,
constant `memset`, or opcode substitution, and does not scan every file above.
Its zero forbidden-writer result was not proof of universal unification. The
registry now classifies the two TLS APIs as metadata authorities and removes
the old canonical TLS “neutral fixed sequence” exception. The remaining raw
sites are explicit migration work, not hidden behind that counter.

Mach-O dyld bind opcodes, unwind records, hashes, AArch64 words, target-address
payloads and source `.byte` directives are not x86 instruction authorities.
They must not be rewritten merely because a numeric constant resembles an
x86 opcode.

## 2. Authoritative representation and ownership contract

The following describes the target architecture of the metadata system; only
the TLS slice below is implemented by this change. Extend existing records and
generation/import tools incrementally, keeping the generated snapshot versioned.

### Identities and projections

Keep semantic operation identity distinct from encoding form identity. A form
has a stable key/hash plus schema/source revision; a dense form ID or prepared
exact token belongs to one generated snapshot and must not survive regeneration
without validation. ABI compound recipes reference forms rather than copying
opcodes. A selected instruction carries only the compact IDs and dynamic
operands it actually needs.

One canonical normalized form supplies encoding fields, operand legality,
feature predicates and semantic effects. Generate compact views for syntax,
validation, selection, dependency construction, disassembly fixtures, and hot
emission. Different views are useful; independently maintained facts are not.
Keep `MachineInstruction` at its existing 24-byte representation. Put static
facts in opcode/form side tables, not redundant per-instruction fields.

### Prefixes, widths and addresses

Use a tagged encoding-family record for legacy/REX, VEX2/VEX3 and EVEX. Preserve
existing XOP and REX2/APX distinctions rather than squeezing them into ordinary
EVEX or silently losing supported forms. The plan specifies map/opcode,
mandatory-prefix meaning, W/L policy, inverted register-field bindings,
reserved bits, and decorator legality. ABI redundant prefixes are a separate
recipe policy, not an invented opcode variant.

Execution mode, default operand size, effective operand size, effective address
size and stack-address size are separate facts. `0x66` as opcode refinement is
not the same decision as an operand-size override; `0x67` is not inferred from
the loaded value's width. Inputs without a base register still need an explicit
address-size policy. Prefix order and high-byte-register exclusions belong to
validation before any caller buffer is modified.

A shared address plan owns ModRM register versus opcode-extension roles, SIB
scale/index/base roles, register-extension bits, RIP-relative versus absolute
addressing, address-size truncation/extension, and VSIB constraints. Cover
RBP/R13 zero-displacement encodings, RSP/R12 SIB requirements, absent base/index,
legal scales, and AH/BH/CH/DH incompatibility with REX. Never transfer an ignored
input bit into a different output operand role during relaxation.

For EVEX, the displacement-compression divisor comes from the form's tuple and
broadcast/element interpretation. It is not automatically the vector byte
width. Masking, zeroing, broadcast, rounding/SAE and high register bits must be
validated together with the selected form and execution mode.

### Values, alternatives and fixups

Record semantic value width separately from encoded field width and signedness.
A constant's bit pattern, signed immediate, sign-extended immediate and PC-relative
distance are not interchangeable. The plan chooses legal immediate/displacement
widths, including imm8/imm32 boundaries and divisible in-range disp8*N. An
unresolved symbol cannot use a short form merely because its placeholder is zero.

Return a neutral fixup descriptor containing byte offset, field width, value
interpretation, relocation kind, symbol/addend, and the PC base used to compute
relative values. ELF/COFF/Mach-O/JIT adapters select their relocation records and
policies; they do not reconstruct instruction layout. Layout-changing branch
relaxation is an explicit bounded layout operation, not a side effect of emission.

Alternative selection is policy: shortest legal form, exact requested form,
fixed ABI envelope, unresolved-symbol width, or a measured target-specific
preference. Never let a new table row silently change code size through lookup
order. The RAX ADD immediate alternative in the TLS oracle is a concrete reason:
a six-byte accumulator form and a seven-byte group form are both legal, but only
the latter fits this existing seven-byte in-place rewrite contract.

Feature predicates must express conjunctions, alternatives, mode restrictions
and explicit exclusions. Compile them to masks/ranges or small checked programs;
do not repeatedly compare feature strings in the hot emitter. A mnemonic is not
a sufficient feature key. Untrusted source requests use the checked boundary;
prevalidated machine tokens require the same complete operand/feature proof.

### Register constraints, selection and scheduling

Per-operand facts include bank/class/width, fixed and implicit registers, uses,
definitions, ties, early clobbers, alias/subregister effects, flags and memory
side effects. Exact form identity is not semantic operation identity: legacy,
VEX and EVEX alternatives can have different dependency and upper-register
behavior. Derive scheduler dependencies from the selected form's semantics.

Instruction-selection patterns remain target policy over canonical IR. Their
outputs reference legal form families and generated operand predicates instead
of reproducing encoding tests. Scheduling resource/latency/throughput tables are
microarchitecture-specific views keyed by form/effect class, not universal ISA
facts embedded into one latency field. A Zen 5 scheduling model and another
CPU model can share forms without pretending their resource costs are equal.

Source aliases, AT&T operand order/suffixes, size estimation and diagnostics
should become syntax/validation projections. A size query and an emit query must
consume the same resolved plan; they must not independently guess SIB, prefixes,
or immediate widths. Decoder tests use independent oracle bytes and legal-form
sets; a Buster encode/decode round trip alone cannot prove correctness.

The AT&T suffix adapter accepts generic `b/w/l/q` widths. The `s/t` widths
come only from explicit typed aliases, including the x87 aliases. A literal
metadata mnemonic retains its identity after its operands are rejected; a
trailing letter must not turn `bts` into `bt`. Typed aliases such as scalar
`movq` still select their base family when the distinct metadata `MOVQ` family
does not match. These are syntax projections, not another byte authority.

### Throughput and publication

Keep compact contiguous records, integer IDs, immutable normalized plans and
specialized prepared templates. A single authority does not require one large
interpreted switch on every emitted instruction. Cache keys must include all
relevant widths, address mode, feature/decorator policy, form identity and
snapshot generation. Cached field offsets come from the plan, never a second
handwritten instruction description.

Prepare caches serially, publish readiness last, and prewarm before a worker
gang. A failed derivation stays invalid and fails closed. Do not turn an
unsupported form into a raw-byte fallback. Migrate a closed family only after
all of its consumers and opcode-changing relaxations use the same authority.

## 3. Implemented first family: ELF TLS address sequences

The closed slice is the existing x86-64 general-dynamic address sequence and its
local-exec replacement, plus the existing initial-exec ADD relaxation. Ordinary
MOV, LEA, ADD and CALL forms already use metadata in source assembly and both
backends. This migration removes the four TLS-specific exceptions around them;
it does not attempt to migrate every instruction bearing those mnemonics.

`buster_x86_metadata_emit_tls_general_dynamic` and
`buster_x86_metadata_relax_tls` now own the fixed envelopes. Their templates are
derived by the checked metadata encoder using typed operands and symbolic fields.
Initialization verifies exact lengths, fixup offsets/widths/kinds and addends.
Only ABI-required redundant prefix padding is specified directly by the recipe.
No new opcode, ModRM or SIB builder is introduced.

The 16-byte GD envelope has a four-byte TLS address field at offset 4 and the
helper call field at offset 12. Object producers retain TLSGD and PLT32 symbol
policy, with addend -4 for both fields. The replacement is FS MOV to RAX followed
by a fixed-disp32 LEA; its final four bytes contain the thread-pointer offset.
The IE envelope stays seven bytes, and all 16 GPR destinations are derived from
metadata. Small and zero values do not shrink these ABI-sized envelopes.

The implementation prepares GD, local-exec and IE templates independently.
An object-only GD compile derives only its LEA and CALL, not all 36 forms used
across every recipe. Ordinary non-TLS compiles never initialize this cache.
`prewarm_all_forms` prepares every group for parallel test consumers.

Invalid model, null buffer, short capacity or unrecognized shape leaves caller
storage unchanged. Linker section bounds are checked using ordered subtraction
before forming the recipe pointer, instead of overflow-prone `offset + size`.
The existing ADD-based IE replacement is retained; changing it to LEA would be
a separate semantics decision, not an incidental oracle-matching refactor.

### Confirmed correctness bug fixed

The old IE rewrite accepted redundant REX.B on a RIP-relative input and copied
that bit into a register-immediate output, where it selects a different GPR.
The actual old production helper and GNU disassembly reproduce:

```text
input:       49 03 05 00 00 00 00       add 0(%rip), %rax
old output:  49 81 c0 fc ff ff ff       add $-4, %r8
new output:  48 81 c0 fc ff ff ff       add $-4, %rax
```

The new matcher identifies the input form/register and selects the independently
derived output template. Ignored REX.X/B are never propagated into output roles.
This preserves all normal compiler-emitted TLS bytes while correcting previously
accepted redundant-prefix input.

## 4. Oracle and regression contract

`tests/x86_64_tls_encoding_oracle.s` is independent GNU as / LLVM integrated
assembler source. Prefix syntax and `{disp32}` request the ABI envelope; the
fixture does not hide instruction bytes in `.byte` directives. GNU binutils
2.44 and Clang 17 produced identical GD, local-exec and all 16 IE-input forms.
GNU as chooses the six-byte accumulator RAX ADD; LLVM chooses the required
seven-byte group form. Other 15 ADD forms agree. This exception is recorded,
not normalized away or reported as unconditional byte identity.

Reproduce the independent object and relocation inspection from the repository
root (each line is one command):

```sh
as --64 tests/x86_64_tls_encoding_oracle.s -o /tmp/tls-gas.o
clang --target=x86_64-linux-gnu -c tests/x86_64_tls_encoding_oracle.s -o /tmp/tls-llvm.o
readelf -Wr /tmp/tls-gas.o /tmp/tls-llvm.o
objdump -drw /tmp/tls-gas.o /tmp/tls-llvm.o
for section in gd le ie; do objcopy -O binary --only-section=.text.$section /tmp/tls-gas.o /tmp/gas-$section.bin; objcopy -O binary --only-section=.text.$section /tmp/tls-llvm.o /tmp/llvm-$section.bin; cmp /tmp/gas-$section.bin /tmp/llvm-$section.bin || exit; done
sed -n '/^\.section \.text.add/,/^\.section \.note/p' tests/x86_64_tls_encoding_oracle.s > /tmp/tls-add.s
clang --target=x86_64-linux-gnu -c /tmp/tls-add.s -o /tmp/tls-add.o
ld --defsym=tls_offset=-4 -e 0 -Ttext=0 /tmp/tls-add.o -o /tmp/tls-add
objdump -drw /tmp/tls-add
```

The new small `x86_64_tls_test.c` is registered in CMake and the standard test
runner, rather than growing the already enormous metadata test translation unit.
Its 294,552 assertions cover every REX/ModRM pair, all opcode bytes, all 16
registers, four redundant REX variants, every value in each individual field-byte
position, signed and short-displacement boundaries, every GD byte mutation,
64 alignments, capacities 0 through 32, guard bytes and failure atomicity.
This is exhaustive over those finite domains, **not** all 2^32 displacements or
the entire x86 ISA.

Existing driver TLS tests now assert the complete independent GD byte oracle and
paired TLSGD/PLT32 offsets, symbols and addends for all four allocators with and
without PIC. Existing execution tests remain enabled. Cross-target disassembly
checks are reported separately from native execution.

## 5. Next closed migrations

First migrate GOT-load relaxation and forwarding-runtime stubs into validated
form/recipe consumers. Next give assembler and canonical code padding a shared,
metadata-derived NOP policy while retaining bulk fill for long runs and preserving
explicit user data. Then replace assembler size/legality tables family by family
with plan projections, and generate selector/dependency/scheduling views from
shared form/effect records. Each step needs independent bytes, fixups, negative
cases, exact/checked equivalence, all allocator modes and compile-time evidence.

Existing APX/EVEX/suffix correctness issues are not declared fixed by a TLS-only
migration. No support claim should be inferred merely from a schema having a
prefix tag. No migration may delete an active fallback before its covered domain
has a proven replacement, or retain an unreported raw escape after claiming a
family complete.

Primary design references: Intel XED's [encoding interface](https://intelxed.github.io/ref-manual/group__ENC.html)
and [high-level width/address API](https://intelxed.github.io/ref-manual/group__ENCHL.html);
LLVM's [target description and code generation](https://llvm.org/docs/CodeGenerator.html)
and [generated backend views](https://llvm.org/docs/TableGen/BackEnds.html).
They inform the separation of concerns; neither library is added as a Buster
runtime or build dependency by this change.
