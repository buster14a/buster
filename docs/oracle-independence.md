# Oracle independence map

[Tests and CI](agents/testing.md) · Paths are relative to the repository root.

A differential or round-trip test only proves something when its two sides
obtain the crucial answer independently. When both sides read the same
implementation, generated table, layout routine or belief, the test agrees
with itself even when the shared fact is wrong. This map records, for each
test route, where the expected answer comes from, what the two sides share,
and which independent oracle breaks the tie. Update it when a route gains
or loses an independent oracle.

## How to read a route

- **Shared dependency** is the fact both sides take from one place. A
  shared dependency is not a defect by itself, but only an outside oracle
  can validate it.
- **Independent oracle** is an implementation or published specification
  that shares no code or table with Buster: Clang or GCC compiling the same
  source, LLVM's readers and debugger, the Linux verifier, Node's Wasm
  engine, a host C evaluator, or constants copied from a specification with
  a citation.
- A **law** is a relation that must hold across genuinely different
  machinery, for example "the bytes a debugger reads for `s.b` are the bits
  the program stored".

## Routes

| Route | Two sides | Shared dependency | Independent oracle | Status |
|---|---|---|---|---|
| Record layout | `sizeof`/`offsetof` fold (`c_parse_type_layout_core`) and `IrType` layout (`c_lower_to_ir_with_options`) | Formerly two hand-mirrored copies of the System V rule; now one placement authority, `c_record_layout_place`, under the target's `CRecordLayoutRule` | `record_layout_tests`, a Clang-derived corpus for every native target, and `tools/record_layout_oracle.py campaign` | Repaired in #1439 (Windows, #1344 AAPCS64, #1318 `#pragma pack`). Ordinary-member `#pragma pack` versus `aligned` is still open: #1244, #1248. |
| Layout ↔ ABI classification ↔ call lowering | Frontend layout, then `ir.c` classifiers, then MIR lowering | `IrTypeLayout` and `IrField` | Host-compiler cross-links in `tools/differential.c` and the driver ABI fixtures | Independent on native hosts only. Cross-target classification is Buster against Buster. |
| Debug information | `dwarf.c`/`codeview.c`/`pdb.c` writers and their tests' own parsers | The tests restated the writer's constants and beliefs | Spec constants in `dwarf_test.c`, `codeview_test.c` and `debug_test.c`, plus `tools/debug_info_oracle.py` (lldb, llvm-dwarfdump, llvm-readobj) | Repaired in #1440: array subranges, bit-field geometry, CodeView register numbers, `LF_ULONG`, `LF_ARRAY` size, PDB numeric leaves. |
| DWARF CFI ↔ prologue | `dwarf_cfi_build` and the prologue emitter | Both come from `CodegenUnwindAction` | libgcc's `_Unwind_Backtrace` restoring callee-saved registers above a Buster frame | Probed on x86-64 Linux: realigned, VLA, `alloca`, 100 KB, variadic and nested frames in all four allocators pass. AArch64 is not probed yet. |
| Direct compiler ↔ self-hosted generations | C0, C1 and C2 | Every frontend and IR rule, since the compiler compiles itself | Host Clang/GCC-compiled subjects with separately compiled observers | A fixed point preserves shared semantic defects (#1352). It is not a semantic oracle. |
| Constant folding ↔ run time | Seven compile-time evaluators and generated code | Literal and character decoders, `target_data_layout` | Host C evaluator (metamorphic), Clang objects (`long double` initializers), hand values | Many open disagreements: #1238, #1226, #1246, #1230, #1347. |
| x86-64 encoder ↔ census/metadata | Source assembler and metadata emitter | The XED snapshot `x86_64-assembly.generated.h` | Literal GNU as / llvm-mc bytes in `assembly_test.c` and `x86_64_metadata_test.c` | Independent only where literal bytes exist. The census alone is circular. |
| AArch64 decoder ↔ patcher/semantic VM | `a64_mc_decode`, relocation patching, VM round trip | One descriptor table and `arm-a64-canonical.generated.jsonl` | llvm-mc literal tables and native execution on arm64 lanes | The VM round trip is circular by construction. |
| Object writer ↔ reader/linker | `object.c` writers and readers, `link.c` | Hand-kept relocation constants | Checked against the published ELF/COFF/Mach-O values (all correct); system linkers on native lanes; `coff-extended-relocations.yml` | Conventions that only Buster reads remain untested, for example Mach-O x86-64 PC-relative data written as `BRANCH`. |
| eBPF encoder ↔ test VM | `ebpf.c` and `ebpf_test_vm.h` | None at the opcode level: the VM uses raw literals, and every opcode matches RFC 9669 | Host C formulas | Semantic gaps the VM cannot see are #1305. |
| Native ↔ LLVM ↔ Wasm ↔ eBPF (metamorphic) | Four backends | Canonical IR | A separately written host evaluator plus Clang/GCC at `-O0`/`-O2` | Independent. The grammar is unsigned-only. |
| Predefined macros ↔ layout | `c_source.c` predefines and the layout engines | `TargetDataLayout` | `clang -target T -E` | Divergences are filed as #1253. |

## Laws with an executable oracle

- **Record layout equals the target ABI.** For every target, the size,
  alignment, ordinary-member offsets and bit occupancy of every record equal
  Clang's. They are observed as static data images, so no execution is
  needed: `tools/record_layout_oracle.py`, `record_layout_tests`.
- **Folded layout equals object layout.** This holds by construction, now
  that both engines call `c_record_layout_place`. The corpus observes both
  sides separately: enumerator folds and emitted objects.
- **Bit-field access equals Clang's.** Stores, read-back and byte images
  through generated code equal Clang's for random records, including
  `packed` and `#pragma pack`, in all four allocators. The repair campaign
  ran this on x86-64 Linux; the driver fixture's Windows and AArch64 copies
  execute on those CI lanes.
- **A debugger reads what the program stored.** lldb evaluates arrays and
  bit-fields in a Buster executable exactly as in Clang's, and
  llvm-readobj/llvm-dwarfdump describe the same geometry:
  `tools/debug_info_oracle.py`.
- **Unwinding restores what the epilogue restores.** libgcc's unwinder,
  walking through a Buster frame, recovers the caller's callee-saved
  registers.

## Rules for new tests

- Never generate one side's expected values from another Buster path. A
  golden file needs provenance: the independent tool and version that
  produced it, and the command to regenerate it.
- Fail closed. A missing symbol, section or consumer is a failure or an
  explicit skip, never a comparison of two absent answers. The first
  version of the layout oracle compared `None == None` for Mach-O and would
  have reported a vacuous pass.
- Sample the facts where a wrong rule and the right one disagree. CodeView
  registers tested only at RAX and R15, the two registers where "hardware
  order plus 328" agrees with the specification, could not detect the wrong
  mapping.
